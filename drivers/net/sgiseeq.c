/* $Id: sgiseeq.c,v 1.17 2000/03/27 23:02:57 ralf Exp $
 *
 * sgiseeq.c: Seeq8003 ethernet driver for SGI machines.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <linux/errno.h>
#include <asm/byteorder.h>

#include <linux/socket.h>
#include <linux/route.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/sgi/sgihpc.h>
#include <asm/sgi/sgint23.h>
#include <asm/sgialib.h>

#include "sgiseeq.h"

static char *version =
	"sgiseeq.c: David S. Miller (dm@engr.sgi.com)\n";

static char *sgiseeqstr = "SGI Seeq8003";

/* If you want speed, you do something silly, it always has worked
 * for me.  So, with that in mind, I've decided to make this driver
 * look completely like a stupid Lance from a driver architecture
 * perspective.  Only difference is that here our "ring buffer" looks
 * and acts like a real Lance one does but is layed out like how the
 * HPC DMA and the Seeq want it to.  You'd be surprised how a stupid
 * idea like this can pay off in performance, not to mention making
 * this driver 2,000 times easier to write. ;-)
 */

/* Tune these if we tend to run out often etc. */
#define SEEQ_RX_BUFFERS  16
#define SEEQ_TX_BUFFERS  16

#define PKT_BUF_SZ       1584

#define NEXT_RX(i)  (((i) + 1) & (SEEQ_RX_BUFFERS - 1))
#define NEXT_TX(i)  (((i) + 1) & (SEEQ_TX_BUFFERS - 1))
#define PREV_RX(i)  (((i) - 1) & (SEEQ_RX_BUFFERS - 1))
#define PREV_TX(i)  (((i) - 1) & (SEEQ_TX_BUFFERS - 1))

#define TX_BUFFS_AVAIL(sp) ((sp->tx_old <= sp->tx_new) ? \
			    sp->tx_old + (SEEQ_TX_BUFFERS - 1) - sp->tx_new : \
			    sp->tx_old - sp->tx_new - 1)

#define DEBUG

struct sgiseeq_rx_desc {
	struct hpc_dma_desc rdma;
	signed int buf_vaddr;
};

struct sgiseeq_tx_desc {
	struct hpc_dma_desc tdma;
	signed int buf_vaddr;
};

/* Warning: This structure is layed out in a certain way because
 *          HPC dma descriptors must be 8-byte aligned.  So don't
 *          touch this without some care.
 */
struct sgiseeq_init_block { /* Note the name ;-) */
	/* Ptrs to the descriptors in KSEG1 uncached space. */
	struct sgiseeq_rx_desc *rx_desc;
	struct sgiseeq_tx_desc *tx_desc;
	unsigned int _padding[30]; /* Pad out to largest cache line size. */

	struct sgiseeq_rx_desc rxvector[SEEQ_RX_BUFFERS];
	struct sgiseeq_tx_desc txvector[SEEQ_TX_BUFFERS];
};

struct sgiseeq_private {
	volatile struct sgiseeq_init_block srings;
	char *name;
	volatile struct hpc3_ethregs *hregs;
	volatile struct sgiseeq_regs *sregs;

	/* Ring entry counters. */
	unsigned int rx_new, tx_new;
	unsigned int rx_old, tx_old;

	int is_edlc;
	unsigned char control;
	unsigned char mode;

	struct net_device_stats stats;
};

static inline void hpc3_eth_reset(volatile struct hpc3_ethregs *hregs)
{
	hregs->rx_reset = (HPC3_ERXRST_CRESET | HPC3_ERXRST_CLRIRQ);
	udelay(20);
	hregs->rx_reset = 0;
}

static inline void reset_hpc3_and_seeq(volatile struct hpc3_ethregs *hregs,
				       volatile struct sgiseeq_regs *sregs)
{
	hregs->rx_ctrl = hregs->tx_ctrl = 0;
	hpc3_eth_reset(hregs);
}

#define RSTAT_GO_BITS (SEEQ_RCMD_IGOOD | SEEQ_RCMD_IEOF | SEEQ_RCMD_ISHORT | \
		       SEEQ_RCMD_IDRIB | SEEQ_RCMD_ICRC)

static inline void seeq_go(struct sgiseeq_private *sp,
			   volatile struct hpc3_ethregs *hregs,
			   volatile struct sgiseeq_regs *sregs)
{
	sregs->rstat = sp->mode | RSTAT_GO_BITS;
	hregs->rx_ctrl = HPC3_ERXCTRL_ACTIVE;
}

static inline void seeq_load_eaddr(struct net_device *dev,
				   volatile struct sgiseeq_regs *sregs)
{
	int i;

	sregs->tstat = SEEQ_TCMD_RB0;
	for (i = 0; i < 6; i++)
		sregs->rw.eth_addr[i] = dev->dev_addr[i];
}

#define TCNTINFO_INIT (HPCDMA_EOX | HPCDMA_ETXD)
#define RCNTCFG_INIT  (HPCDMA_OWN | HPCDMA_EORP | HPCDMA_XIE)
#define RCNTINFO_INIT (RCNTCFG_INIT | (PKT_BUF_SZ & HPCDMA_BCNT))

static void seeq_init_ring(struct net_device *dev)
{
	struct sgiseeq_private *sp = (struct sgiseeq_private *) dev->priv;
	volatile struct sgiseeq_init_block *ib = &sp->srings;
	int i;

	netif_stop_queue(dev);
	sp->rx_new = sp->tx_new = 0;
	sp->rx_old = sp->tx_old = 0;

	seeq_load_eaddr(dev, sp->sregs);

	/* XXX for now just accept packets directly to us
	 * XXX and ether-broadcast.  Will do multicast and
	 * XXX promiscuous mode later. -davem
	 */
	sp->mode = SEEQ_RCMD_RBCAST;

	/* Setup tx ring. */
	for(i = 0; i < SEEQ_TX_BUFFERS; i++) {
		if(!ib->tx_desc[i].tdma.pbuf) {
			unsigned long buffer;

			buffer = (unsigned long) kmalloc(PKT_BUF_SZ, GFP_KERNEL);
			ib->tx_desc[i].buf_vaddr = KSEG1ADDR(buffer);
			ib->tx_desc[i].tdma.pbuf = PHYSADDR(buffer);
//			flush_cache_all();
		}
		ib->tx_desc[i].tdma.cntinfo = (TCNTINFO_INIT);
	}

	/* And now the rx ring. */
	for (i = 0; i < SEEQ_RX_BUFFERS; i++) {
		if (!ib->rx_desc[i].rdma.pbuf) {
			unsigned long buffer;

			buffer = (unsigned long) kmalloc(PKT_BUF_SZ, GFP_KERNEL);
			ib->rx_desc[i].buf_vaddr = KSEG1ADDR(buffer);
			ib->rx_desc[i].rdma.pbuf = PHYSADDR(buffer);
//			flush_cache_all();
		}
		ib->rx_desc[i].rdma.cntinfo = (RCNTINFO_INIT);
	}
	ib->rx_desc[i - 1].rdma.cntinfo |= (HPCDMA_EOR);
}

#ifdef DEBUG
static struct sgiseeq_private *gpriv;
static struct net_device *gdev;

void sgiseeq_dump_rings(void)
{
	static int once = 0;
	struct sgiseeq_rx_desc *r = gpriv->srings.rx_desc;
	struct sgiseeq_tx_desc *t = gpriv->srings.tx_desc;
	volatile struct hpc3_ethregs *hregs = gpriv->hregs;
	int i;

	if(once)
		return;
	once++;
	printk("RING DUMP:\n");
	for (i = 0; i < SEEQ_RX_BUFFERS; i++) {
		printk("RX [%d]: @(%p) [%08x,%08x,%08x] ",
		       i, (&r[i]), r[i].rdma.pbuf, r[i].rdma.cntinfo,
		       r[i].rdma.pnext);
		i += 1;
		printk("-- [%d]: @(%p) [%08x,%08x,%08x]\n",
		       i, (&r[i]), r[i].rdma.pbuf, r[i].rdma.cntinfo,
		       r[i].rdma.pnext);
	}
	for (i = 0; i < SEEQ_TX_BUFFERS; i++) {
		printk("TX [%d]: @(%p) [%08x,%08x,%08x] ",
		       i, (&t[i]), t[i].tdma.pbuf, t[i].tdma.cntinfo,
		       t[i].tdma.pnext);
		i += 1;
		printk("-- [%d]: @(%p) [%08x,%08x,%08x]\n",
		       i, (&t[i]), t[i].tdma.pbuf, t[i].tdma.cntinfo,
		       t[i].tdma.pnext);
	}
	printk("INFO: [rx_new = %d rx_old=%d] [tx_new = %d tx_old = %d]\n",
	       gpriv->rx_new, gpriv->rx_old, gpriv->tx_new, gpriv->tx_old);
	printk("RREGS: rx_cbptr[%08x] rx_ndptr[%08x] rx_ctrl[%08x]\n",
	       hregs->rx_cbptr, hregs->rx_ndptr, hregs->rx_ctrl);
	printk("TREGS: tx_cbptr[%08x] tx_ndptr[%08x] tx_ctrl[%08x]\n",
	       hregs->tx_cbptr, hregs->tx_ndptr, hregs->tx_ctrl);
}
#endif

#define TSTAT_INIT_SEEQ (SEEQ_TCMD_IPT|SEEQ_TCMD_I16|SEEQ_TCMD_IC|SEEQ_TCMD_IUF)
#define TSTAT_INIT_EDLC ((TSTAT_INIT_SEEQ) | SEEQ_TCMD_RB2)
#define RDMACFG_INIT    (HPC3_ERXDCFG_FRXDC | HPC3_ERXDCFG_FEOP | HPC3_ERXDCFG_FIRQ)

static void init_seeq(struct net_device *dev, struct sgiseeq_private *sp,
		      volatile struct sgiseeq_regs *sregs)
{
	volatile struct hpc3_ethregs *hregs = sp->hregs;

	reset_hpc3_and_seeq(hregs, sregs);
	seeq_init_ring(dev);

	/* Setup to field the proper interrupt types. */
	if (sp->is_edlc) {
		sregs->tstat = (TSTAT_INIT_EDLC);
		sregs->rw.wregs.control = sp->control;
		sregs->rw.wregs.frame_gap = 0;
	} else {
		sregs->tstat = (TSTAT_INIT_SEEQ);
	}

	hregs->rx_dconfig |= RDMACFG_INIT;

	hregs->rx_ndptr = PHYSADDR(&sp->srings.rx_desc[0]);
	hregs->tx_ndptr = PHYSADDR(&sp->srings.tx_desc[0]);

	seeq_go(sp, hregs, sregs);
}

static inline void record_rx_errors(struct sgiseeq_private *sp,
				    unsigned char status)
{
	if (status & SEEQ_RSTAT_OVERF ||
	    status & SEEQ_RSTAT_SFRAME)
		sp->stats.rx_over_errors++;
	if (status & SEEQ_RSTAT_CERROR)
		sp->stats.rx_crc_errors++;
	if (status & SEEQ_RSTAT_DERROR)
		sp->stats.rx_frame_errors++;
	if (status & SEEQ_RSTAT_REOF)
		sp->stats.rx_errors++;
}

static inline void rx_maybe_restart(struct sgiseeq_private *sp,
				    volatile struct hpc3_ethregs *hregs,
				    volatile struct sgiseeq_regs *sregs)
{
	if (!(hregs->rx_ctrl & HPC3_ERXCTRL_ACTIVE)) {
		hregs->rx_ndptr = PHYSADDR(&sp->srings.rx_desc[sp->rx_new]);
		seeq_go(sp, hregs, sregs);
	}
}

#define for_each_rx(rd, sp) for((rd) = &(sp)->srings.rx_desc[(sp)->rx_new]; \
				!((rd)->rdma.cntinfo & HPCDMA_OWN); \
				(rd) = &(sp)->srings.rx_desc[(sp)->rx_new])

static inline void sgiseeq_rx(struct net_device *dev, struct sgiseeq_private *sp,
			      volatile struct hpc3_ethregs *hregs,
			      volatile struct sgiseeq_regs *sregs)
{
	struct sgiseeq_rx_desc *rd;
	struct sk_buff *skb = 0;
	unsigned char pkt_status;
	unsigned char *pkt_pointer = 0;
	int len = 0;
	unsigned int orig_end = PREV_RX(sp->rx_new);

	/* Service every received packet. */
	for_each_rx(rd, sp) {
		len = (PKT_BUF_SZ - (rd->rdma.cntinfo & HPCDMA_BCNT) - 3);
		pkt_pointer = (unsigned char *)(long)rd->buf_vaddr;
		pkt_status = pkt_pointer[len + 2];

		if (pkt_status & SEEQ_RSTAT_FIG) {
			/* Packet is OK. */
			skb = dev_alloc_skb(len + 2);

			if (skb) {
				skb->dev = dev;
				skb_reserve(skb, 2);
				skb_put(skb, len);

				/* Copy out of kseg1 to avoid silly cache flush. */
				eth_copy_and_sum(skb, pkt_pointer + 2, len, 0);
				skb->protocol = eth_type_trans(skb, dev);
				netif_rx(skb);
				sp->stats.rx_packets++;
				sp->stats.rx_bytes += len;
			} else {
				printk ("%s: Memory squeeze, deferring packet.\n",
					dev->name);
				sp->stats.rx_dropped++;
			}
		} else {
			record_rx_errors(sp, pkt_status);
		}

		/* Return the entry to the ring pool. */
		rd->rdma.cntinfo = (RCNTINFO_INIT);
		sp->rx_new = NEXT_RX(sp->rx_new);
	}
	sp->srings.rx_desc[orig_end].rdma.cntinfo &= ~(HPCDMA_EOR);
	sp->srings.rx_desc[PREV_RX(sp->rx_new)].rdma.cntinfo |= HPCDMA_EOR;
	rx_maybe_restart(sp, hregs, sregs);
}

static inline void tx_maybe_reset_collisions(struct sgiseeq_private *sp,
					     volatile struct sgiseeq_regs *sregs)
{
	if (sp->is_edlc) {
		sregs->rw.wregs.control = sp->control & ~(SEEQ_CTRL_XCNT);
		sregs->rw.wregs.control = sp->control;
	}
}

static inline void kick_tx(struct sgiseeq_tx_desc *td,
			   volatile struct hpc3_ethregs *hregs)
{
	/* If the HPC aint doin nothin, and there are more packets
	 * with ETXD cleared and XIU set we must make very certain
	 * that we restart the HPC else we risk locking up the
	 * adapter.  The following code is only safe iff the HPCDMA
	 * is not active!
	 */
	while ((td->tdma.cntinfo & (HPCDMA_XIU | HPCDMA_ETXD)) ==
	      (HPCDMA_XIU | HPCDMA_ETXD))
		td = (struct sgiseeq_tx_desc *)(long) KSEG1ADDR(td->tdma.pnext);
	if (td->tdma.cntinfo & HPCDMA_XIU) {
		hregs->tx_ndptr = PHYSADDR(td);
		hregs->tx_ctrl = HPC3_ETXCTRL_ACTIVE;
	}
}

static inline void sgiseeq_tx(struct net_device *dev, struct sgiseeq_private *sp,
			      volatile struct hpc3_ethregs *hregs,
			      volatile struct sgiseeq_regs *sregs)
{
	struct sgiseeq_tx_desc *td;
	unsigned long status = hregs->tx_ctrl;
	int j;

	tx_maybe_reset_collisions(sp, sregs);

	if (!(status & (HPC3_ETXCTRL_ACTIVE | SEEQ_TSTAT_PTRANS))) {
		/* Oops, HPC detected some sort of error. */
		if (status & SEEQ_TSTAT_R16)
			sp->stats.tx_aborted_errors++;
		if (status & SEEQ_TSTAT_UFLOW)
			sp->stats.tx_fifo_errors++;
		if (status & SEEQ_TSTAT_LCLS)
			sp->stats.collisions++;
	}

	/* Ack 'em... */
	for (j = sp->tx_old; j != sp->tx_new; j = NEXT_TX(j)) {
		td = &sp->srings.tx_desc[j];

		if (!(td->tdma.cntinfo & (HPCDMA_XIU)))
			break;
		if (!(td->tdma.cntinfo & (HPCDMA_ETXD))) {
			if(!(status & HPC3_ETXCTRL_ACTIVE)) {
				hregs->tx_ndptr = PHYSADDR(td);
				hregs->tx_ctrl = HPC3_ETXCTRL_ACTIVE;
			}
			break;
		}
		sp->stats.tx_packets++;
		sp->tx_old = NEXT_TX(sp->tx_old);
		td->tdma.cntinfo &= ~(HPCDMA_XIU | HPCDMA_XIE);
		td->tdma.cntinfo |= HPCDMA_EOX;
	}
}

static void sgiseeq_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct sgiseeq_private *sp = (struct sgiseeq_private *) dev->priv;
	volatile struct hpc3_ethregs *hregs = sp->hregs;
	volatile struct sgiseeq_regs *sregs = sp->sregs;

	/* Ack the IRQ and set software state. */
	hregs->rx_reset = HPC3_ERXRST_CLRIRQ;

	/* Always check for received packets. */
	sgiseeq_rx(dev, sp, hregs, sregs);

	/* Only check for tx acks iff we have something queued. */
	if (sp->tx_old != sp->tx_new)
		sgiseeq_tx(dev, sp, hregs, sregs);

	if ((TX_BUFFS_AVAIL(sp) > 0) && netif_queue_stopped(dev)) {
		netif_wake_queue(dev);
	}
}

static int sgiseeq_open(struct net_device *dev)
{
	struct sgiseeq_private *sp = (struct sgiseeq_private *)dev->priv;
	volatile struct sgiseeq_regs *sregs = sp->sregs;
	unsigned long flags;

	save_flags(flags); cli();
	if (request_irq(dev->irq, sgiseeq_interrupt, 0, sgiseeqstr, (void *) dev)) {
		printk("Seeq8003: Can't get irq %d\n", dev->irq);
		restore_flags(flags);
		return -EAGAIN;
	}

	init_seeq(dev, sp, sregs);

	netif_start_queue(dev);
	restore_flags(flags);
	return 0;
}

static int sgiseeq_close(struct net_device *dev)
{
	struct sgiseeq_private *sp = (struct sgiseeq_private *) dev->priv;
	volatile struct sgiseeq_regs *sregs = sp->sregs;

	netif_stop_queue(dev);

	/* Shutdown the Seeq. */
	reset_hpc3_and_seeq(sp->hregs, sregs);

	free_irq(dev->irq, dev);

	return 0;
}

static inline int sgiseeq_reset(struct net_device *dev)
{
	struct sgiseeq_private *sp = (struct sgiseeq_private *) dev->priv;
	volatile struct sgiseeq_regs *sregs = sp->sregs;

	init_seeq(dev, sp, sregs);

	dev->trans_start = jiffies;
	netif_wake_queue(dev);

	return 0;
}

void sgiseeq_my_reset(void)
{
	printk("RESET!\n");
	sgiseeq_reset(gdev);
}

static int sgiseeq_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct sgiseeq_private *sp = (struct sgiseeq_private *) dev->priv;
	volatile struct hpc3_ethregs *hregs = sp->hregs;
	unsigned long flags;
	struct sgiseeq_tx_desc *td;
	int skblen, len, entry;

	save_and_cli(flags);

	/* Setup... */
	skblen = skb->len;
	len = (skblen <= ETH_ZLEN) ? ETH_ZLEN : skblen;
	sp->stats.tx_bytes += len;
	entry = sp->tx_new;
	td = &sp->srings.tx_desc[entry];

	/* Create entry.  There are so many races with adding a new
	 * descriptor to the chain:
	 * 1) Assume that the HPC is off processing a DMA chain while
	 *    we are changing all of the following.
	 * 2) Do no allow the HPC to look at a new descriptor until
	 *    we have completely set up it's state.  This means, do
	 *    not clear HPCDMA_EOX in the current last descritptor
	 *    until the one we are adding looks consistant and could
	 *    be processes right now.
	 * 3) The tx interrupt code must notice when we've added a new
	 *    entry and the HPC got to the end of the chain before we
	 *    added this new entry and restarted it.
	 */
	memcpy((char *)(long)td->buf_vaddr, skb->data, skblen);
	td->tdma.cntinfo = (len & HPCDMA_BCNT) |
	                   (HPCDMA_XIU | HPCDMA_EOXP | HPCDMA_XIE | HPCDMA_EOX);
	if (sp->tx_old != sp->tx_new) {
		struct sgiseeq_tx_desc *backend;

		backend = &sp->srings.tx_desc[PREV_TX(sp->tx_new)];
		backend->tdma.cntinfo &= ~(HPCDMA_EOX);
	}
	sp->tx_new = NEXT_TX(sp->tx_new); /* Advance. */

	/* Maybe kick the HPC back into motion. */
	if (!(hregs->tx_ctrl & HPC3_ETXCTRL_ACTIVE))
		kick_tx(&sp->srings.tx_desc[sp->tx_old], hregs);

	dev->trans_start = jiffies;
	dev_kfree_skb(skb);

	if (!TX_BUFFS_AVAIL(sp))
		netif_stop_queue(dev);
	restore_flags(flags);

	return 0;
}

static void timeout(struct net_device *dev)
{
	printk("%s: transmit timed out, resetting\n", dev->name);
	sgiseeq_reset(dev);

	dev->trans_start = jiffies;
	netif_wake_queue(dev);
}

static struct net_device_stats *sgiseeq_get_stats(struct net_device *dev)
{
	struct sgiseeq_private *sp = (struct sgiseeq_private *) dev->priv;

	return &sp->stats;
}

static void sgiseeq_set_multicast(struct net_device *dev)
{
}

static inline void setup_tx_ring(struct sgiseeq_tx_desc *buf, int nbufs)
{
	int i = 0;

	while (i < (nbufs - 1)) {
		buf[i].tdma.pnext = PHYSADDR(&buf[i + 1]);
		buf[i].tdma.pbuf = 0;
		i++;
	}
	buf[i].tdma.pnext = PHYSADDR(&buf[0]);
}

static inline void setup_rx_ring(struct sgiseeq_rx_desc *buf, int nbufs)
{
	int i = 0;

	while (i < (nbufs - 1)) {
		buf[i].rdma.pnext = PHYSADDR(&buf[i + 1]);
		buf[i].rdma.pbuf = 0;
		i++;
	}
	buf[i].rdma.pbuf = 0;
	buf[i].rdma.pnext = PHYSADDR(&buf[0]);
}

static char onboard_eth_addr[6];

#define ALIGNED(x)  ((((unsigned long)(x)) + 0xf) & ~(0xf))

int sgiseeq_init(struct net_device *dev, struct sgiseeq_regs *sregs,
		 struct hpc3_ethregs *hregs, int irq)
{
	static unsigned version_printed = 0;
	int i;
	struct sgiseeq_private *sp;

	dev->priv = (struct sgiseeq_private *) get_free_page(GFP_KERNEL);
	if (dev->priv == NULL)
		return -ENOMEM;

	if (!version_printed++)
		printk(version);

	printk("%s: SGI Seeq8003 ", dev->name);

	for (i = 0; i < 6; i++)
		printk("%2.2x%c",
		       dev->dev_addr[i] = onboard_eth_addr[i],
		       i == 5 ? ' ': ':');

	printk("\n");

	sp = (struct sgiseeq_private *) dev->priv;
#ifdef DEBUG
	gpriv = sp;
	gdev = dev;
#endif
	memset((char *)dev->priv, 0, sizeof(struct sgiseeq_private));
	sp->sregs = sregs;
	sp->hregs = hregs;
	sp->name = sgiseeqstr;

	sp->srings.rx_desc = (struct sgiseeq_rx_desc *)
	                     (KSEG1ADDR(ALIGNED(&sp->srings.rxvector[0])));
	dma_cache_wback_inv((unsigned long)&sp->srings.rxvector,
	                    sizeof(sp->srings.rxvector));
	sp->srings.tx_desc = (struct sgiseeq_tx_desc *)
	                     (KSEG1ADDR(ALIGNED(&sp->srings.txvector[0])));
	dma_cache_wback_inv((unsigned long)&sp->srings.txvector,
	                    sizeof(sp->srings.txvector));

	/* A couple calculations now, saves many cycles later. */
	setup_rx_ring(sp->srings.rx_desc, SEEQ_RX_BUFFERS);
	setup_tx_ring(sp->srings.tx_desc, SEEQ_TX_BUFFERS);

	/* Reset the chip. */
	hpc3_eth_reset((volatile struct hpc3_ethregs *) hregs);

	sp->is_edlc = !(sregs->rw.rregs.collision_tx[0] & 0xff);
	if (sp->is_edlc) {
		sp->control = (SEEQ_CTRL_XCNT | SEEQ_CTRL_ACCNT |
			       SEEQ_CTRL_SFLAG | SEEQ_CTRL_ESHORT |
			       SEEQ_CTRL_ENCARR);
	}

	dev->open                 = sgiseeq_open;
	dev->stop                 = sgiseeq_close;
	dev->hard_start_xmit      = sgiseeq_start_xmit;
	dev->tx_timeout           = timeout;
	dev->watchdog_timeo       = (200 * HZ) / 1000;
	dev->get_stats            = sgiseeq_get_stats;
	dev->set_multicast_list   = sgiseeq_set_multicast;
	dev->irq                  = irq;
	dev->dma                  = 0;
	ether_setup(dev);

	return 0;
}

static inline unsigned char str2hexnum(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return 0; /* foo */
}

static inline void str2eaddr(unsigned char *ea, unsigned char *str)
{
	int i;

	for (i = 0; i < 6; i++) {
		unsigned char num;

		if(*str == ':')
			str++;
		num = str2hexnum(*str++) << 4;
		num |= (str2hexnum(*str++));
		ea[i] = num;
	}
}

int sgiseeq_probe(struct net_device *dev)
{
	static int initialized = 0;
	char *ep;

	if (initialized)	/* Already initialized? */
		return 1;
	initialized++;

	/* First get the ethernet address of the onboard interface from ARCS.
	 * This is fragile; PROM doesn't like running from cache.
	 * On MIPS64 it crashes for some other, yet unknown reason ...
	 */
	ep = ArcGetEnvironmentVariable("eaddr");
	str2eaddr(onboard_eth_addr, ep);
	return sgiseeq_init(dev,
			    (struct sgiseeq_regs *) (KSEG1ADDR(0x1fbd4000)),
			    &hpc3c0->ethregs, SGI_ENET_IRQ);
}
