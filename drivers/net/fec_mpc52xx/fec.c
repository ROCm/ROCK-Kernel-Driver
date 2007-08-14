/*
 * drivers/net/fec_mpc52xx/fec.c
 *
 * Driver for the MPC5200 Fast Ethernet Controller
 *
 * Author: Dale Farnsworth <dfarnsworth@mvista.com>
 *
 * 2003-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/crc32.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>

#include <asm/io.h>
#include <asm/delay.h>
#include <asm/ppcboot.h>
#include <asm/mpc52xx.h>

#if defined(CONFIG_PPC_MERGE)
#include <asm/of_device.h>
#include <asm/of_platform.h>
#include <platforms/52xx/bestcomm.h>
#include "sdma_fec.h"
#else
#include <syslib/bestcomm/bestcomm.h>
#include <syslib/bestcomm/fec.h>
#endif

#include "fec_phy.h"
#include "fec.h"

#define DRIVER_NAME "mpc52xx-fec"

static irqreturn_t fec_interrupt(int, void *);
static irqreturn_t fec_rx_interrupt(int, void *);
static irqreturn_t fec_tx_interrupt(int, void *);
static struct net_device_stats *fec_get_stats(struct net_device *);
static void fec_set_multicast_list(struct net_device *dev);
static void fec_reinit(struct net_device *dev);

static u8 mpc52xx_fec_mac_addr[6];
static u8 null_mac[6];

static void fec_tx_timeout(struct net_device *dev)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;

	priv->stats.tx_errors++;

	if (!priv->tx_full)
		netif_wake_queue(dev);
}

static void fec_set_paddr(struct net_device *dev, u8 *mac)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	struct mpc52xx_fec *fec = priv->fec;

	out_be32(&fec->paddr1, *(u32*)(&mac[0]));
	out_be32(&fec->paddr2, (*(u16*)(&mac[4]) << 16) | 0x8808);
}

static void fec_get_paddr(struct net_device *dev, u8 *mac)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	struct mpc52xx_fec *fec = priv->fec;

	*(u32*)(&mac[0]) = in_be32(&fec->paddr1);
	*(u16*)(&mac[4]) = in_be32(&fec->paddr2) >> 16;
}

static int fec_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sock = (struct sockaddr *)addr;

	memcpy(dev->dev_addr, sock->sa_data, dev->addr_len);

	fec_set_paddr(dev, sock->sa_data);
	return 0;
}

/* This function is called to start or restart the FEC during a link
 * change.  This happens on fifo errors or when switching between half
 * and full duplex.
 */
static void fec_restart(struct net_device *dev, int duplex)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	struct mpc52xx_fec *fec = priv->fec;
	u32 rcntrl;
	u32 tcntrl;
	int i;

	out_be32(&fec->rfifo_status, in_be32(&fec->rfifo_status) & 0x700000);
	out_be32(&fec->tfifo_status, in_be32(&fec->tfifo_status) & 0x700000);
	out_be32(&fec->reset_cntrl, 0x1000000);

	/* Whack a reset.  We should wait for this. */
	out_be32(&fec->ecntrl, FEC_ECNTRL_RESET);
	for (i = 0; i < FEC_RESET_DELAY; ++i) {
		if ((in_be32(&fec->ecntrl) & FEC_ECNTRL_RESET) == 0)
			break;
		udelay(1);
	}
	if (i == FEC_RESET_DELAY)
		printk (KERN_ERR DRIVER_NAME ": FEC Reset timeout!\n");

	/* Set station address. */
	fec_set_paddr(dev, dev->dev_addr);

	fec_set_multicast_list(dev);

	rcntrl = FEC_RX_BUFFER_SIZE << 16;	/* max frame length */
	rcntrl |= FEC_RCNTRL_FCE;
	rcntrl |= MII_RCNTL_MODE;
	if (duplex)
		tcntrl = FEC_TCNTRL_FDEN;		/* FD enable */
	else {
		rcntrl |= FEC_RCNTRL_DRT;
		tcntrl = 0;
	}
	out_be32(&fec->r_cntrl, rcntrl);
	out_be32(&fec->x_cntrl, tcntrl);

	set_phy_speed(fec, priv->phy_speed);

	priv->full_duplex = duplex;

	/* Clear any outstanding interrupt. */
	out_be32(&fec->ievent, 0xffffffff);	/* clear intr events */

	/* Enable interrupts we wish to service.
	*/
	out_be32(&fec->imask, FEC_IMASK_ENABLE);

	/* And last, enable the transmit and receive processing.
	*/
	out_be32(&fec->ecntrl, FEC_ECNTRL_ETHER_EN);
	out_be32(&fec->r_des_active, 0x01000000);

	/* The tx ring is no longer full. */
	if (priv->tx_full)
	{
		priv->tx_full = 0;
		netif_wake_queue(dev);
	}
}

static void fec_free_rx_buffers(struct sdma *s)
{
	struct sk_buff *skb;

	while (!sdma_queue_empty(s)) {
		skb = sdma_retrieve_buffer(s, NULL);
		kfree_skb(skb);
	}
}

static int fec_open(struct net_device *dev)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	struct sk_buff *skb;
	void *data;

	sdma_fec_rx_init(priv->rx_sdma, priv->rx_fifo, FEC_RX_BUFFER_SIZE);
	sdma_fec_tx_init(priv->tx_sdma, priv->tx_fifo);

	while (!sdma_queue_full(priv->rx_sdma)) {
		skb = dev_alloc_skb(FEC_RX_BUFFER_SIZE);
		if (skb == 0)
			goto eagain;

		/* zero out the initial receive buffers to aid debugging */
		memset(skb->data, 0, FEC_RX_BUFFER_SIZE);
		data = (void *)virt_to_phys(skb->data);
		sdma_submit_buffer(priv->rx_sdma, skb, data, FEC_RX_BUFFER_SIZE);
	}

	fec_set_paddr(dev, dev->dev_addr);

	if (fec_mii_wait(dev) != 0)
		return -ENODEV;

	sdma_enable(priv->rx_sdma);
	sdma_enable(priv->tx_sdma);

	netif_start_queue(dev);

	return 0;

eagain:
	printk(KERN_ERR "fec_open: failed\n");

	fec_free_rx_buffers(priv->rx_sdma);

	return -EAGAIN;
}

/* This will only be invoked if your driver is _not_ in XOFF state.
 * What this means is that you need not check it, and that this
 * invariant will hold if you make sure that the netif_*_queue()
 * calls are done at the proper times.
 */
static int fec_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	void *data;

	if (sdma_queue_full(priv->tx_sdma))
		panic("MPC52xx transmit queue overrun\n");

	spin_lock_irq(&priv->lock);
	dev->trans_start = jiffies;

	data = (void *)virt_to_phys(skb->data);
	sdma_fec_tfd_submit_buffer(priv->tx_sdma, skb, data, skb->len);

	if (sdma_queue_full(priv->tx_sdma)) {
		priv->tx_full = 1;
		netif_stop_queue(dev);
	}
	spin_unlock_irq(&priv->lock);

	return 0;
}

/* This handles BestComm transmit task interrupts
 */
static irqreturn_t fec_tx_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	struct sk_buff *skb;

	for (;;) {
		sdma_clear_irq(priv->tx_sdma);
		spin_lock(&priv->lock);
		if (!sdma_buffer_done(priv->tx_sdma)) {
			spin_unlock(&priv->lock);
			break;
		}
		skb = sdma_retrieve_buffer(priv->tx_sdma, NULL);

		if (priv->tx_full) {
			priv->tx_full = 0;
			netif_wake_queue(dev);
		}
		spin_unlock(&priv->lock);
		dev_kfree_skb_irq(skb);
	}

	return IRQ_HANDLED;
}

static irqreturn_t fec_rx_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	struct sk_buff *skb;
	struct sk_buff *rskb;
	int status;

	for (;;) {
		sdma_clear_irq(priv->rx_sdma);

		if (!sdma_buffer_done(priv->rx_sdma))
			break;

		rskb = sdma_retrieve_buffer(priv->rx_sdma, &status);

		/* Test for errors in received frame */
		if (status & 0x370000) {
			/* Drop packet and reuse the buffer */
			sdma_submit_buffer(
				priv->rx_sdma, rskb,
				(void *)virt_to_phys(rskb->data),
				FEC_RX_BUFFER_SIZE );

			priv->stats.rx_dropped++;

			continue;
		}

		/* allocate replacement skb */
		skb = dev_alloc_skb(FEC_RX_BUFFER_SIZE);
		if (skb) {
			/* Process the received skb */
			int length = (status & ((1<<11) - 1)) - sizeof(u32);
			skb_put(rskb, length);	/* length included CRC32 */

			rskb->dev = dev;
			rskb->protocol = eth_type_trans(rskb, dev);
			netif_rx(rskb);
			dev->last_rx = jiffies;
		} else {
			/* Can't get a new one : reuse the same & drop pkt */
			printk(KERN_NOTICE
				"%s: Memory squeeze, dropping packet.\n",
				dev->name);
			priv->stats.rx_dropped++;

			skb = rskb;
		}

		sdma_submit_buffer( priv->rx_sdma, skb,
			(void *)virt_to_phys(skb->data), FEC_RX_BUFFER_SIZE );
	}

	return IRQ_HANDLED;
}

static irqreturn_t fec_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	struct mpc52xx_fec *fec = priv->fec;
	int ievent;

	ievent = in_be32(&fec->ievent);
	if (!ievent)
		return IRQ_NONE;

	out_be32(&fec->ievent, ievent);		/* clear pending events */

	if (ievent & (FEC_IEVENT_RFIFO_ERROR | FEC_IEVENT_XFIFO_ERROR)) {
		if (netif_running(dev) && net_ratelimit() && (ievent & FEC_IEVENT_RFIFO_ERROR))
			printk(KERN_WARNING "FEC_IEVENT_RFIFO_ERROR (%.8x)\n",
			       ievent);
		if (netif_running(dev) && net_ratelimit() && (ievent & FEC_IEVENT_XFIFO_ERROR))
			printk(KERN_WARNING "FEC_IEVENT_XFIFO_ERROR (%.8x)\n",
			       ievent);
		fec_reinit(dev);
	}
	else if (ievent & FEC_IEVENT_MII)
		fec_mii(dev);
	return IRQ_HANDLED;
}

static int fec_close(struct net_device *dev)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	unsigned long timeout;

	priv->open_time = 0;
	priv->sequence_done = 0;

	netif_stop_queue(dev);

	sdma_disable(priv->rx_sdma);		/* disable receive task */

	/* Wait for queues to drain */
	timeout = jiffies + 2*HZ;
	while (time_before(jiffies, timeout) &&
					(!sdma_queue_empty(priv->tx_sdma) ||
					!sdma_queue_empty(priv->rx_sdma))) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ/10);
	}
	if (time_after_eq(jiffies, timeout))
		printk(KERN_ERR "fec_close: queues didn't drain\n");

	sdma_disable(priv->tx_sdma);

	fec_free_rx_buffers(priv->rx_sdma);

	fec_get_stats(dev);

	return 0;
}

/*
 * Get the current statistics.
 * This may be called with the card open or closed.
 */
static struct net_device_stats *fec_get_stats(struct net_device *dev)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	struct net_device_stats *stats = &priv->stats;
	struct mpc52xx_fec *fec = priv->fec;

	stats->rx_bytes = in_be32(&fec->rmon_r_octets);
	stats->rx_packets = in_be32(&fec->rmon_r_packets);
	stats->rx_errors = stats->rx_packets - in_be32(&fec->ieee_r_frame_ok);
	stats->tx_bytes = in_be32(&fec->rmon_t_octets);
	stats->tx_packets = in_be32(&fec->rmon_t_packets);
	stats->tx_errors = stats->tx_packets - (
					in_be32(&fec->ieee_t_frame_ok) +
					in_be32(&fec->rmon_t_col) +
					in_be32(&fec->ieee_t_1col) +
					in_be32(&fec->ieee_t_mcol) +
					in_be32(&fec->ieee_t_def));
	stats->multicast = in_be32(&fec->rmon_r_mc_pkt);
	stats->collisions = in_be32(&fec->rmon_t_col);

	/* detailed rx_errors: */
	stats->rx_length_errors = in_be32(&fec->rmon_r_undersize)
					+ in_be32(&fec->rmon_r_oversize)
					+ in_be32(&fec->rmon_r_frag)
					+ in_be32(&fec->rmon_r_jab);
	stats->rx_over_errors = in_be32(&fec->r_macerr);
	stats->rx_crc_errors = in_be32(&fec->ieee_r_crc);
	stats->rx_frame_errors = in_be32(&fec->ieee_r_align);
	stats->rx_fifo_errors = in_be32(&fec->rmon_r_drop);
	stats->rx_missed_errors = in_be32(&fec->rmon_r_drop);

	/* detailed tx_errors: */
	stats->tx_aborted_errors = 0;
	stats->tx_carrier_errors = in_be32(&fec->ieee_t_cserr);
	stats->tx_fifo_errors = in_be32(&fec->rmon_t_drop);
	stats->tx_heartbeat_errors = in_be32(&fec->ieee_t_sqe);
	stats->tx_window_errors = in_be32(&fec->ieee_t_lcol);

	return stats;
}

static void fec_update_stat(struct net_device *dev)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	struct net_device_stats *stats = &priv->stats;
	struct mpc52xx_fec *fec = priv->fec;

	out_be32(&fec->mib_control, FEC_MIB_DISABLE);
	memset_io(&fec->rmon_t_drop, 0,
			(u32)&fec->reserved10 - (u32)&fec->rmon_t_drop);
	out_be32(&fec->mib_control, 0);
	memset(stats, 0, sizeof *stats);
	fec_get_stats(dev);
}

/*
 * Set or clear the multicast filter for this adaptor.
 */
static void fec_set_multicast_list(struct net_device *dev)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	struct mpc52xx_fec *fec = priv->fec;
	u32 rx_control;

	rx_control = in_be32(&fec->r_cntrl);

	if (dev->flags & IFF_PROMISC) {
		rx_control |= FEC_RCNTRL_PROM;
		out_be32(&fec->r_cntrl, rx_control);
	} else {
		rx_control &= ~FEC_RCNTRL_PROM;
		out_be32(&fec->r_cntrl, rx_control);

		if (dev->flags & IFF_ALLMULTI) {
			out_be32(&fec->gaddr1, 0xffffffff);
			out_be32(&fec->gaddr2, 0xffffffff);
		} else {
			u32 crc;
			int i;
			struct dev_mc_list *dmi;
			u32 gaddr1 = 0x00000000;
			u32 gaddr2 = 0x00000000;

			dmi = dev->mc_list;
			for (i=0; i<dev->mc_count; i++) {
				crc = ether_crc_le(6, dmi->dmi_addr) >> 26;
				if (crc >= 32)
					gaddr1 |= 1 << (crc-32);
				else
					gaddr2 |= 1 << crc;
				dmi = dmi->next;
			}
			out_be32(&fec->gaddr1, gaddr1);
			out_be32(&fec->gaddr2, gaddr2);
		}
	}
}

static void __init fec_str2mac(char *str, unsigned char *mac)
{
	int i;
	u64 val64;

	val64 = simple_strtoull(str, NULL, 16);

	for (i = 0; i < 6; i++)
		mac[5-i] = val64 >> (i*8);
}

int __init mpc52xx_fec_mac_setup(char *mac_address)
{
	fec_str2mac(mac_address, mpc52xx_fec_mac_addr);
	return 0;
}

__setup("mpc52xx-mac=", mpc52xx_fec_mac_setup);

static void fec_hw_init(struct net_device *dev)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	struct mpc52xx_fec *fec = priv->fec;
#if !defined(CONFIG_PPC_MERGE)
	bd_t *bd = (bd_t *) &__res;
#endif

	out_be32(&fec->op_pause, 0x00010020);
	out_be32(&fec->rfifo_cntrl, 0x0f000000);
	out_be32(&fec->rfifo_alarm, 0x0000030c);
	out_be32(&fec->tfifo_cntrl, 0x0f000000);
	out_be32(&fec->tfifo_alarm, 0x00000100);
	out_be32(&fec->x_wmrk, 0x3);		/* xmit fifo watermark = 256 */
	out_be32(&fec->xmit_fsm, 0x03000000);	/* enable crc generation */
	out_be32(&fec->iaddr1, 0x00000000);	/* No individual filter */
	out_be32(&fec->iaddr2, 0x00000000);	/* No individual filter */

#if !defined(CONFIG_PPC_MERGE)
	priv->phy_speed = ((bd->bi_ipbfreq >> 20) / 5) << 1;
#endif

	fec_restart(dev, 0);	/* always use half duplex mode only */
	/*
	 * Read MIB counters in order to reset them,
	 * then zero all the stats fields in memory
	 */
	fec_update_stat(dev);
}


static void fec_reinit(struct net_device *dev)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	struct mpc52xx_fec *fec = priv->fec;

	netif_stop_queue(dev);
	out_be32(&fec->imask, 0x0);

	/* Disable the rx and tx tasks. */
	sdma_disable(priv->rx_sdma);
	sdma_disable(priv->tx_sdma);

	/* Stop FEC */
	out_be32(&fec->ecntrl, in_be32(&fec->ecntrl) & ~0x2);

	/* Restart the DMA tasks */
	sdma_fec_rx_init(priv->rx_sdma, priv->rx_fifo, FEC_RX_BUFFER_SIZE);
	sdma_fec_tx_init(priv->tx_sdma, priv->tx_fifo);
	fec_hw_init(dev);

	if (priv->sequence_done) {		 /* redo the fec_open() */
		fec_free_rx_buffers(priv->rx_sdma);
		fec_open(dev);
	}
	return;
}


/* ======================================================================== */
/* Platform Driver                                                               */
/* ======================================================================== */

#if defined(CONFIG_PPC_MERGE)
static int __devinit
mpc52xx_fec_probe(struct of_device *op, const struct of_device_id *match)
#else
static int __devinit
mpc52xx_fec_probe(struct device *dev)
#endif
{
	int ret;
#if defined(CONFIG_PPC_MERGE)
	int rv;
	struct resource __mem;
	struct resource *mem = &__mem;
#else
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *mem;
#endif
	struct net_device *ndev;
	struct fec_priv *priv = NULL;

	volatile int dbg=0;
	while(dbg)
		__asm("nop");
	/* Reserve FEC control zone */
#if defined(CONFIG_PPC_MERGE)
	rv = of_address_to_resource(op->node, 0, mem);
	if (rv) {
		printk(KERN_ERR DRIVER_NAME ": "
			"Error while parsing device node resource\n" );
		return rv;
	}
#else
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if ((mem->end - mem->start + 1) != sizeof(struct mpc52xx_fec)) {
		printk(KERN_ERR DRIVER_NAME
			   " - invalid resource size (%lx != %x), check mpc52xx_devices.c\n",
									mem->end - mem->start + 1, sizeof(struct mpc52xx_fec));
		return -EINVAL;
	}
#endif

	if (!request_mem_region(mem->start, sizeof(struct mpc52xx_fec),
	                        DRIVER_NAME))
		return -EBUSY;

	/* Get the ether ndev & it's private zone */
	ndev = alloc_etherdev(sizeof(struct fec_priv));
	if (!ndev) {
		printk(KERN_ERR DRIVER_NAME ": "
			"Can not allocate the ethernet device\n" );
		ret = -ENOMEM;
		goto probe_error;
	}

	priv = (struct fec_priv *)ndev->priv;

	/* Init ether ndev with what we have */
	ndev->open		= fec_open;
	ndev->stop		= fec_close;
	ndev->hard_start_xmit	= fec_hard_start_xmit;
	ndev->do_ioctl		= fec_ioctl;
	ndev->get_stats		= fec_get_stats;
	ndev->set_mac_address	= fec_set_mac_address;
	ndev->set_multicast_list = fec_set_multicast_list;
	ndev->tx_timeout	= fec_tx_timeout;
	ndev->watchdog_timeo	= FEC_WATCHDOG_TIMEOUT;
	ndev->flags &= ~IFF_RUNNING;
	ndev->base_addr		= mem->start;

	priv->rx_fifo = ndev->base_addr + FIELD_OFFSET(mpc52xx_fec,rfifo_data);
	priv->tx_fifo = ndev->base_addr + FIELD_OFFSET(mpc52xx_fec,tfifo_data);
	priv->t_irq = priv->r_irq = ndev->irq = -1; /* IRQ are free for now */

	spin_lock_init(&priv->lock);

	/* ioremap the zones */
	priv->fec = (struct mpc52xx_fec *)
		ioremap(mem->start, sizeof(struct mpc52xx_fec));

	if (!priv->fec) {
		printk(KERN_ERR DRIVER_NAME ": "
			"Can not remap IO memory at 0x%8.8x\n", mem->start );
		ret = -ENOMEM;
		goto probe_error;
	}

	/* SDMA init */
	priv->rx_sdma = sdma_alloc(FEC_RX_NUM_BD);
	priv->tx_sdma = sdma_alloc(FEC_TX_NUM_BD);

	if (!priv->rx_sdma || !priv->tx_sdma) {
		printk(KERN_ERR DRIVER_NAME ": "
			"Can not init SDMA tasks\n" );
		ret = -ENOMEM;
		goto probe_error;
	}

	ret = sdma_fec_rx_init(priv->rx_sdma, priv->rx_fifo,FEC_RX_BUFFER_SIZE);
	if (ret < 0)
		goto probe_error;

	ret = sdma_fec_tx_init(priv->tx_sdma, priv->tx_fifo);
	if (ret < 0)
		goto probe_error;

	/* Get the IRQ we need one by one */
	/* Control */
#if defined(CONFIG_PPC_MERGE)
	ndev->irq = irq_of_parse_and_map(op->node, 0);
#else
	ndev->irq = platform_get_irq(pdev, 0);
#endif

	if (request_irq(ndev->irq, &fec_interrupt, SA_INTERRUPT,
	                DRIVER_NAME "_ctrl", ndev)) {
		printk(KERN_ERR DRIVER_NAME ": ctrl interrupt request failed\n");
		ret = -EBUSY;
		ndev->irq = -1;	/* Don't try to free it */
		goto probe_error;
	}

	/* RX */
	priv->r_irq = sdma_irq(priv->rx_sdma);
	if (request_irq(priv->r_irq, &fec_rx_interrupt, SA_INTERRUPT,
	                DRIVER_NAME "_rx", ndev)) {
		printk(KERN_ERR DRIVER_NAME ": rx request_irq(0x%x) failed\n",
		       priv->r_irq);
		ret = -EBUSY;
		priv->r_irq = -1;	/* Don't try to free it */
		goto probe_error;
	}

	/* TX */
	priv->t_irq = sdma_irq(priv->tx_sdma);
	if (request_irq(priv->t_irq, &fec_tx_interrupt, SA_INTERRUPT,
	                DRIVER_NAME "_tx", ndev)) {
		printk(KERN_ERR DRIVER_NAME ": tx request_irq(0x%x) failed\n",
		       priv->t_irq);
		ret = -EBUSY;
		priv->t_irq = -1;	/* Don't try to free it */
		goto probe_error;
	}

#if defined(CONFIG_PPC_MERGE)
	priv->phy_speed = ((mpc52xx_find_ipb_freq(op->node) >> 20) / 5) << 1;
#endif

	/* MAC address init */
	if (memcmp(mpc52xx_fec_mac_addr, null_mac, 6) != 0)
		memcpy(ndev->dev_addr, mpc52xx_fec_mac_addr, 6);
	else
		fec_get_paddr(ndev, ndev->dev_addr);

	/* Hardware init */
	fec_hw_init(ndev);

	SET_NETDEV_DEV(ndev, &op->dev);
	/* Register the new network device */
	ret = register_netdev(ndev);
	if(ret < 0)
		goto probe_error;

	/* MII init : After register ???? */
	fec_mii_init(ndev);

	/* We're done ! */
	printk(KERN_INFO "%s: mpc52xx-fec at %#lx\n",
	       ndev->name, (long)mem->start);
#if defined(CONFIG_PPC_MERGE)
	dev_set_drvdata(&op->dev, ndev);
#else
	dev_set_drvdata(dev, ndev);
#endif

	return 0;


	/* Error handling - free everything that might be allocated */
probe_error:

	if (ndev) {
		if (priv->rx_sdma)	sdma_free(priv->rx_sdma);
		if (priv->tx_sdma)	sdma_free(priv->tx_sdma);

		if (ndev->irq >= 0)	free_irq(ndev->irq, ndev);
		if (priv->r_irq >= 0)	free_irq(priv->r_irq, ndev);
		if (priv->t_irq >= 0)	free_irq(priv->t_irq, ndev);

		if (priv->fec)		iounmap(priv->fec);

		free_netdev(ndev);
	}

	release_mem_region(mem->start, sizeof(struct mpc52xx_fec));

	return ret;
}

#if defined(CONFIG_PPC_MERGE)
static int
mpc52xx_fec_remove(struct of_device *op)
#else
static int
mpc52xx_fec_remove(struct device *dev)
#endif
{
	struct net_device *ndev;
	struct fec_priv *priv;

#if defined(CONFIG_PPC_MERGE)
	ndev = (struct net_device *) dev_get_drvdata(&op->dev);
#else
	ndev = (struct net_device *) dev_get_drvdata(dev);
#endif
	if (!ndev)
		return 0;
	priv = (struct fec_priv *) ndev->priv;

	unregister_netdev(ndev);

	free_irq(ndev->irq, ndev);
	free_irq(priv->r_irq, ndev);
	free_irq(priv->t_irq, ndev);

	iounmap(priv->fec);

	release_mem_region(ndev->base_addr, sizeof(struct mpc52xx_fec));

	free_netdev(ndev);

#if defined(CONFIG_PPC_MERGE)
	dev_set_drvdata(&op->dev, NULL);
#else
	dev_set_drvdata(dev, NULL);
#endif
	return 0;
}

#if defined(CONFIG_PPC_MERGE)
static struct of_device_id mpc52xx_fec_of_match[] = {
	{ .compatible = "mpc5200-ethernet", },
	{ .compatible = "mpc5200-fec", },
	{},
};
MODULE_DEVICE_TABLE(of, mpc52xx_fec_of_match);

static struct of_platform_driver mpc52xx_fec_driver = {
	.name = DRIVER_NAME,
	.owner = THIS_MODULE,
	.match_table = mpc52xx_fec_of_match,
	.probe = mpc52xx_fec_probe,
	.remove = mpc52xx_fec_remove,
#ifdef CONFIG_PM
/*	.suspend = mpc52xx_fec_suspend, TODO */
/*	.resume = mpc52xx_fec_resume, TODO */
#endif
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};
#else
static struct device_driver mpc52xx_fec_driver = {
	.name	  = DRIVER_NAME,
	.bus		= &platform_bus_type,
	.probe		= mpc52xx_fec_probe,
	.remove		= mpc52xx_fec_remove,
#ifdef CONFIG_PM
/*	.suspend	= mpc52xx_fec_suspend,	TODO */
/*	.resume		= mpc52xx_fec_resume,	TODO */
#endif
};
#endif

/* ======================================================================== */
/* Module                                                                   */
/* ======================================================================== */

static int __init
mpc52xx_fec_init(void)
{
#if defined(CONFIG_PPC_MERGE)
	return of_register_platform_driver(&mpc52xx_fec_driver);
#else
	return driver_register(&mpc52xx_fec_driver);
#endif
}

static void __exit
mpc52xx_fec_exit(void)
{
#if defined(CONFIG_PPC_MERGE)
	of_unregister_platform_driver(&mpc52xx_fec_driver);
#else
	driver_unregister(&mpc52xx_fec_driver);
#endif
}


module_init(mpc52xx_fec_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dale Farnsworth");
MODULE_DESCRIPTION("Ethernet driver for the Freescale MPC52xx FEC");
