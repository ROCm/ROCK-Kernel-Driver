/*
 * drivers/net/wan/dscc4/dscc4_main.c: a DSCC4 HDLC driver for Linux
 *
 * This software may be used and distributed according to the terms of the 
 * GNU General Public License. 
 *
 * The author may be reached as romieu@cogenit.fr.
 * Specific bug reports/asian food will be welcome.
 *
 * Special thanks to the nice people at CS-Telecom for the hardware and the
 * access to the test/measure tools.
 *
 *
 *                             Theory of Operation
 *
 * I. Board Compatibility
 *
 * This device driver is designed for the Siemens PEB20534 4 ports serial
 * controller as found on Etinc PCISYNC cards. The documentation for the 
 * chipset is available at http://www.infineon.com:
 * - Data Sheet "DSCC4, DMA Supported Serial Communication Controller with
 * 4 Channels, PEB 20534 Version 2.1, PEF 20534 Version 2.1";
 * - Application Hint "Management of DSCC4 on-chip FIFO resources".
 * Jens David has built an adapter based on the same chipset. Take a look
 * at http://www.afthd.tu-darmstadt.de/~dg1kjd/pciscc4 for a specific
 * driver.
 * Sample code (2 revisions) is available at Infineon.
 *
 * II. Board-specific settings
 *
 * Pcisync can transmit some clock signal to the outside world on the
 * *first two* ports provided you put a quartz and a line driver on it and
 * remove the jumpers. The operation is described on Etinc web site. If you
 * go DCE on these ports, don't forget to use an adequate cable.
 *
 * Sharing of the PCI interrupt line for this board is possible.
 *
 * III. Driver operation
 *
 * The rx/tx operations are based on a linked list of descriptor. I haven't
 * tried the start/stop descriptor method as this one looks like the cheapest
 * in terms of PCI manipulation.
 *
 * Tx direction
 * Once the data section of the current descriptor processed, the next linked
 * descriptor is loaded if the HOLD bit isn't set in the current descriptor.
 * If HOLD is met, the transmission is stopped until the host unsets it and
 * signals the change via TxPOLL.
 * When the tx ring is full, the xmit routine issues a call to netdev_stop.
 * The device is supposed to be enabled again during an ALLS irq (we could
 * use HI but as it's easy to loose events, it's fscked).
 *
 * Rx direction
 * The received frames aren't supposed to span over multiple receiving areas.
 * I may implement it some day but it isn't the highest ranked item.
 *
 * IV. Notes
 * The chipset is buggy. Typically, under some specific load patterns (I
 * wouldn't call them "high"), the irq queues and the descriptors look like
 * some event has been lost. Even assuming some fancy PCI feature, it won't 
 * explain the reproductible missing "C" bit in the descriptors. Faking an 
 * irq in the periodic timer isn't really elegant but at least it seems 
 * reliable.
 * The current error (XDU, RFO) recovery code is untested.
 * So far, RDO takes his RX channel down and the right sequence to enable it
 * again is still a mistery. If RDO happens, plan a reboot. More details
 * in the code (NB: as this happens, TX still works).
 * Don't mess the cables during operation, especially on DTE ports. I don't
 * suggest it for DCE either but at least one can get some messages instead
 * of a complete instant freeze.
 * Tests are done on Rev. 20 of the silicium. The RDO handling changes with
 * the documentation/chipset releases. An on-line errata would be welcome.
 *
 * TODO:
 * - some trivial error lurk,
 * - the stats are fscked,
 * - use polling at high irq/s,
 * - performance analysis,
 * - endianness.
 *
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/cache.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <linux/init.h>
#include <linux/string.h>

#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <net/syncppp.h>
#include <linux/hdlc.h>

/* Version */
static const char version[] = "$Id: dscc4.c,v 1.130 2001/02/25 15:27:34 romieu Exp $\n";
static int debug;


/* Module parameters */
MODULE_AUTHOR("Maintainer: Francois Romieu <romieu@cogenit.fr>");
MODULE_DESCRIPTION("Siemens PEB20534 PCI Controller");
MODULE_LICENSE("GPL");
MODULE_PARM(debug,"i");

/* Structures */
struct TxFD {
	u32 state;
	u32 next;
	u32 data;
	u32 complete;
	u32 jiffies; /* more hack to come :o) */
};

struct RxFD {
	u32 state1;
	u32 next;
	u32 data;
	u32 state2;
	u32 end;
};

#define DEBUG
#define DEBUG_PARANOID
#define TX_RING_SIZE    32
#define RX_RING_SIZE    32
#define IRQ_RING_SIZE   64 /* Keep it A multiple of 32 */
#define TX_TIMEOUT      (HZ/10)
#define BRR_DIVIDER_MAX 64*0x00008000
#define dev_per_card	4

#define SOURCE_ID(flags) ((flags >> 28 ) & 0x03)
#define TO_SIZE(state) ((state >> 16) & 0x1fff)
#define TO_STATE(len) cpu_to_le32((len & TxSizeMax) << 16)
#define RX_MAX(len) ((((len) >> 5) + 1) << 5)
#define SCC_REG_START(id) SCC_START+(id)*SCC_OFFSET

#undef DEBUG

struct dscc4_pci_priv {
        u32 *iqcfg;
        int cfg_cur;
        spinlock_t lock;
        struct pci_dev *pdev;

        struct net_device *root;
        dma_addr_t iqcfg_dma;
	u32 xtal_hz;
};

struct dscc4_dev_priv {
        struct sk_buff *rx_skbuff[RX_RING_SIZE];
        struct sk_buff *tx_skbuff[TX_RING_SIZE];

        struct RxFD *rx_fd;
        struct TxFD *tx_fd;
        u32 *iqrx;
        u32 *iqtx;

        u32 rx_current;
        u32 tx_current;
        u32 iqrx_current;
        u32 iqtx_current;

        u32 tx_dirty;
	int bad_tx_frame;
	int bad_rx_frame;
	int rx_needs_refill;

        dma_addr_t tx_fd_dma;
        dma_addr_t rx_fd_dma;
        dma_addr_t iqtx_dma;
        dma_addr_t iqrx_dma;

        struct net_device_stats stats;
	struct timer_list timer;

        struct dscc4_pci_priv *pci_priv;
        spinlock_t lock;

        int dev_id;
	u32 flags;
	u32 timer_help;
	u32 hi_expected;

	struct hdlc_device_struct hdlc;
	int usecount;
};

/* GLOBAL registers definitions */
#define GCMDR   0x00
#define GSTAR   0x04
#define GMODE   0x08
#define IQLENR0 0x0C
#define IQLENR1 0x10
#define IQRX0   0x14
#define IQTX0   0x24
#define IQCFG   0x3c
#define FIFOCR1 0x44
#define FIFOCR2 0x48
#define FIFOCR3 0x4c
#define FIFOCR4 0x34
#define CH0CFG  0x50
#define CH0BRDA 0x54
#define CH0BTDA 0x58

/* SCC registers definitions */
#define SCC_START	0x0100
#define SCC_OFFSET      0x80
#define CMDR    0x00
#define STAR    0x04
#define CCR0    0x08
#define CCR1    0x0c
#define CCR2    0x10
#define BRR     0x2C
#define RLCR    0x40
#define IMR     0x54
#define ISR     0x58

/* Bit masks */
#define IntRxScc0       0x10000000
#define IntTxScc0       0x01000000

#define TxPollCmd	0x00000400
#define RxActivate	0x08000000
#define MTFi		0x04000000
#define Rdr		0x00400000
#define Rdt		0x00200000
#define Idr		0x00100000
#define Idt		0x00080000
#define TxSccRes       0x01000000
#define RxSccRes       0x00010000
#define TxSizeMax	0x1ffc
#define RxSizeMax	0x1ffc

#define Ccr0ClockMask	0x0000003f
#define Ccr1LoopMask	0x00000200
#define BrrExpMask	0x00000f00
#define BrrMultMask	0x0000003f
#define EncodingMask	0x00700000
#define Hold		0x40000000
#define SccBusy		0x10000000
#define FrameOk		(FrameVfr | FrameCrc)
#define FrameVfr	0x80
#define FrameRdo	0x40
#define FrameCrc	0x20
#define FrameAborted	0x00000200
#define FrameEnd	0x80000000
#define DataComplete	0x40000000
#define LengthCheck	0x00008000
#define SccEvt		0x02000000
#define NoAck		0x00000200
#define Action		0x00000001
#define HiDesc		0x20000000

/* SCC events */
#define RxEvt		0xf0000000
#define TxEvt		0x0f000000
#define Alls		0x00040000
#define Xdu		0x00010000
#define Xmr		0x00002000
#define Xpr		0x00001000
#define Rdo		0x00000080
#define Rfs		0x00000040
#define Rfo		0x00000002
#define Flex		0x00000001

/* DMA core events */
#define Cfg		0x00200000
#define Hi		0x00040000
#define Fi		0x00020000
#define Err		0x00010000
#define Arf		0x00000002
#define ArAck		0x00000001

/* Misc */
#define NeedIDR		0x00000001
#define NeedIDT		0x00000002
#define RdoSet		0x00000004

/* Functions prototypes */
static __inline__ void dscc4_rx_irq(struct dscc4_pci_priv *, struct net_device *);
static __inline__ void dscc4_tx_irq(struct dscc4_pci_priv *, struct net_device *);
static int dscc4_found1(struct pci_dev *, unsigned long ioaddr);
static int dscc4_init_one(struct pci_dev *, const struct pci_device_id *ent);
static int dscc4_open(struct net_device *);
static int dscc4_start_xmit(struct sk_buff *, struct net_device *);
static int dscc4_close(struct net_device *);
static int dscc4_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int dscc4_change_mtu(struct net_device *dev, int mtu);
static int dscc4_init_ring(struct net_device *);
static void dscc4_release_ring(struct dscc4_dev_priv *);
static void dscc4_timer(unsigned long);
static void dscc4_tx_timeout(struct net_device *);
static void dscc4_irq(int irq, void *dev_id, struct pt_regs *ptregs);
static struct net_device_stats *dscc4_get_stats(struct net_device *);
static int dscc4_attach_hdlc_device(struct net_device *);
static void dscc4_unattach_hdlc_device(struct net_device *);
static int dscc4_hdlc_open(struct hdlc_device_struct *);
static void dscc4_hdlc_close(struct hdlc_device_struct *);
static int dscc4_hdlc_ioctl(struct hdlc_device_struct *, struct ifreq *, int);
static int dscc4_hdlc_xmit(hdlc_device *, struct sk_buff *);
#ifdef EXPERIMENTAL_POLLING
static int dscc4_tx_poll(struct dscc4_dev_priv *, struct net_device *);
#endif

void inline reset_TxFD(struct TxFD *tx_fd) {
	/* FIXME: test with the last arg (size specification) = 0 */
	tx_fd->state = FrameEnd | Hold | 0x00100000;
	tx_fd->complete = 0x00000000;
}

void inline dscc4_release_ring_skbuff(struct sk_buff **p, int n)
{
	for(; n > 0; n--) {
		if (*p)
			dev_kfree_skb(*p);
		p++;
	}
}

static void dscc4_release_ring(struct dscc4_dev_priv *dpriv)
{
	struct pci_dev *pdev = dpriv->pci_priv->pdev;

	pci_free_consistent(pdev, TX_RING_SIZE*sizeof(struct TxFD),
			    dpriv->tx_fd, dpriv->tx_fd_dma);
	pci_free_consistent(pdev, RX_RING_SIZE*sizeof(struct RxFD),
			    dpriv->rx_fd, dpriv->rx_fd_dma);
	dscc4_release_ring_skbuff(dpriv->tx_skbuff, TX_RING_SIZE);
	dscc4_release_ring_skbuff(dpriv->rx_skbuff, RX_RING_SIZE);
}

void inline try_get_rx_skb(struct dscc4_dev_priv *priv, int cur, struct net_device *dev)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(RX_MAX(HDLC_MAX_MRU+2));
	priv->rx_skbuff[cur] = skb;
	if (!skb) {
		priv->rx_fd[cur--].data = (u32) NULL;
		priv->rx_fd[cur%RX_RING_SIZE].state1 |= Hold;
		priv->rx_needs_refill++;
		return;
	}
	skb->dev = dev;
	skb->protocol = htons(ETH_P_IP);
	skb->mac.raw = skb->data;
	priv->rx_fd[cur].data = pci_map_single(priv->pci_priv->pdev, skb->data,
					       skb->len, PCI_DMA_FROMDEVICE);
}

/*
 * IRQ/thread/whatever safe
 */
static int dscc4_wait_ack_cec(u32 ioaddr, struct net_device *dev, char *msg)
{
	s16 i = 0;

	while (readl(ioaddr + STAR) & SccBusy) {
		if (i++ < 0)  {
			printk(KERN_ERR "%s: %s timeout\n", dev->name, msg);
			return -1;
		}
	}
	printk(KERN_DEBUG "%s: %s ack (%d try)\n", dev->name, msg, i);
	return 0;
}

static int dscc4_do_action(struct net_device *dev, char *msg)
{
	unsigned long ioaddr = dev->base_addr;
	u32 state;
	s16 i;

	writel(Action, ioaddr + GCMDR);
	ioaddr += GSTAR;
	for (i = 0; i >= 0; i++) {
		state = readl(ioaddr);
		if (state & Arf) {
			printk(KERN_ERR "%s: %s failed\n", dev->name, msg);
			writel(Arf, ioaddr);
			return -1;
		} else if (state & ArAck) {
			printk(KERN_DEBUG "%s: %s ack (%d try)\n",
			       dev->name, msg, i);
			writel(ArAck, ioaddr);
			return 0;
		}
	}
	printk(KERN_ERR "%s: %s timeout\n", dev->name, msg);
	return -1;
}

static __inline__ int dscc4_xpr_ack(struct dscc4_dev_priv *dpriv)
{
	int cur;
	s16 i;

	cur = dpriv->iqtx_current%IRQ_RING_SIZE;
	for (i = 0; i >= 0; i++) {
		if (!(dpriv->flags & (NeedIDR | NeedIDT)) ||
		    (dpriv->iqtx[cur] & Xpr))
			return 0;
	}
	printk(KERN_ERR "%s: %s timeout\n", "dscc4", "XPR");
	return -1;
}

static __inline__ void dscc4_rx_skb(struct dscc4_dev_priv *dpriv, int cur,
	struct RxFD *rx_fd, struct net_device *dev)
{
	struct pci_dev *pdev = dpriv->pci_priv->pdev;
	struct sk_buff *skb;
	int pkt_len;

	skb = dpriv->rx_skbuff[cur];
	pkt_len = TO_SIZE(rx_fd->state2) - 1;
	pci_dma_sync_single(pdev, rx_fd->data, pkt_len + 1, PCI_DMA_FROMDEVICE);
	if((skb->data[pkt_len] & FrameOk) == FrameOk) {
		pci_unmap_single(pdev, rx_fd->data, skb->len, PCI_DMA_FROMDEVICE);
		dpriv->stats.rx_packets++;
		dpriv->stats.rx_bytes += pkt_len;
		skb->tail += pkt_len;
		skb->len = pkt_len;
       		if (netif_running(hdlc_to_dev(&dpriv->hdlc)))
			hdlc_netif_rx(&dpriv->hdlc, skb);
		else
			netif_rx(skb);
		try_get_rx_skb(dpriv, cur, dev);
	} else {
		if(skb->data[pkt_len] & FrameRdo)
			dpriv->stats.rx_fifo_errors++;
		else if(!(skb->data[pkt_len] | ~FrameCrc))
			dpriv->stats.rx_crc_errors++;
		else if(!(skb->data[pkt_len] | ~FrameVfr))
			dpriv->stats.rx_length_errors++;
		else
			dpriv->stats.rx_errors++;
	}
	rx_fd->state1 |= Hold;
	rx_fd->state2 = 0x00000000;
	rx_fd->end = 0xbabeface;
	if (!rx_fd->data)
		return;
	rx_fd--;
	if (!cur)
		rx_fd += RX_RING_SIZE;
	rx_fd->state1 &= ~Hold;
}

static int __init dscc4_init_one (struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	struct dscc4_pci_priv *priv;
	struct dscc4_dev_priv *dpriv;
	int i;
	static int cards_found = 0;
	unsigned long ioaddr;

	printk(KERN_DEBUG "%s", version);

	if (pci_enable_device(pdev))
		goto err_out;
	if (!request_mem_region(pci_resource_start(pdev, 0),
	                	pci_resource_len(pdev, 0), "registers")) {
	        printk (KERN_ERR "dscc4: can't reserve MMIO region (regs)\n");
	        goto err_out;
	}
	if (!request_mem_region(pci_resource_start(pdev, 1),
	                        pci_resource_len(pdev, 1), "LBI interface")) {
	        printk (KERN_ERR "dscc4: can't reserve MMIO region (lbi)\n");
	        goto err_out_free_mmio_region0;
	}
	ioaddr = (unsigned long)ioremap(pci_resource_start(pdev, 0),
					pci_resource_len(pdev, 0));
	if (!ioaddr) {
		printk(KERN_ERR "dscc4: cannot remap MMIO region %lx @ %lx\n",
	                pci_resource_len(pdev, 0), pci_resource_start(pdev, 0));
		goto err_out_free_mmio_region;
	}
	printk(KERN_DEBUG "Siemens DSCC4, MMIO at %#lx (regs), %#lx (lbi), IRQ %d.\n",
	        pci_resource_start(pdev, 0),
	        pci_resource_start(pdev, 1), pdev->irq);

	/* High PCI latency useless. Cf app. note. */
	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0x10);
	pci_set_master(pdev);

	if (dscc4_found1(pdev, ioaddr))
	        goto err_out_iounmap;

	priv = (struct dscc4_pci_priv *)pci_get_drvdata(pdev);

	if (request_irq(pdev->irq, &dscc4_irq, SA_SHIRQ, "dscc4", priv->root)) {
		printk(KERN_WARNING "dscc4: IRQ %d is busy\n", pdev->irq);
		goto err_out_iounmap;
	}
	priv->pdev = pdev;

	/* power up/little endian/dma core controlled via hold bit */
	writel(0x00000000, ioaddr + GMODE);
	/* Shared interrupt queue */
	{
		u32 bits;

		bits = (IRQ_RING_SIZE >> 5) - 1;
		bits |= bits << 4;
		bits |= bits << 8;
		bits |= bits << 16;
		writel(bits, ioaddr + IQLENR0);
	}
	/* Global interrupt queue */
	writel((u32)(((IRQ_RING_SIZE >> 5) - 1) << 20), ioaddr + IQLENR1);
	priv->iqcfg = (u32 *) pci_alloc_consistent(pdev,
		IRQ_RING_SIZE*sizeof(u32), &priv->iqcfg_dma);
	if (!priv->iqcfg)
		goto err_out_free_irq;
	writel(priv->iqcfg_dma, ioaddr + IQCFG);

	/* 
	 * SCC 0-3 private rx/tx irq structures 
	 * IQRX/TXi needs to be set soon. Learned it the hard way...
	 */
	for(i = 0; i < dev_per_card; i++) {
		dpriv = (struct dscc4_dev_priv *)(priv->root + i)->priv;
		dpriv->iqtx = (u32 *) pci_alloc_consistent(pdev,
			IRQ_RING_SIZE*sizeof(u32), &dpriv->iqtx_dma);
		if (!dpriv->iqtx)
			goto err_out_free_iqtx;
		writel(dpriv->iqtx_dma, ioaddr + IQTX0 + i*4);
	}
	for(i = 0; i < dev_per_card; i++) {
		dpriv = (struct dscc4_dev_priv *)(priv->root + i)->priv;
		dpriv->iqrx = (u32 *) pci_alloc_consistent(pdev,
			IRQ_RING_SIZE*sizeof(u32), &dpriv->iqrx_dma);
		if (!dpriv->iqrx)
			goto err_out_free_iqrx;
		writel(dpriv->iqrx_dma, ioaddr + IQRX0 + i*4);
	}

	/* 
	 * Cf application hint. Beware of hard-lock condition on 
	 * threshold .
	 */
	writel(0x42104000, ioaddr + FIFOCR1);
	//writel(0x9ce69800, ioaddr + FIFOCR2);
	writel(0xdef6d800, ioaddr + FIFOCR2);
	//writel(0x11111111, ioaddr + FIFOCR4);
	writel(0x18181818, ioaddr + FIFOCR4);
	// FIXME: should depend on the chipset revision
	writel(0x0000000e, ioaddr + FIFOCR3);

	writel(0xff200001, ioaddr + GCMDR);

	cards_found++;
	return 0;

err_out_free_iqrx:
	while (--i >= 0) {
		dpriv = (struct dscc4_dev_priv *)(priv->root + i)->priv;
		pci_free_consistent(pdev, IRQ_RING_SIZE*sizeof(u32), 
				    dpriv->iqrx, dpriv->iqrx_dma);
	}
	i = dev_per_card;
err_out_free_iqtx:
	while (--i >= 0) {
		dpriv = (struct dscc4_dev_priv *)(priv->root + i)->priv;
		pci_free_consistent(pdev, IRQ_RING_SIZE*sizeof(u32), 
				    dpriv->iqtx, dpriv->iqtx_dma);
	}
	pci_free_consistent(pdev, IRQ_RING_SIZE*sizeof(u32), priv->iqcfg, 
			    priv->iqcfg_dma);
err_out_free_irq:
	free_irq(pdev->irq, priv->root);
err_out_iounmap:
	iounmap ((void *)ioaddr);
err_out_free_mmio_region:
	release_mem_region(pci_resource_start(pdev, 1),
			   pci_resource_len(pdev, 1));
err_out_free_mmio_region0:
	release_mem_region(pci_resource_start(pdev, 0),
			   pci_resource_len(pdev, 0));
err_out:
	return -ENODEV;
};

static int dscc4_found1(struct pci_dev *pdev, unsigned long ioaddr)
{
	struct dscc4_pci_priv *ppriv;
	struct dscc4_dev_priv *dpriv;
	struct net_device *dev;
	int i = 0;

	dpriv = (struct dscc4_dev_priv *)
		kmalloc(dev_per_card*sizeof(struct dscc4_dev_priv), GFP_KERNEL);
	if (!dpriv) {
		printk(KERN_ERR "dscc4: can't allocate data\n");
		goto err_out;
	}
	memset(dpriv, 0, dev_per_card*sizeof(struct dscc4_dev_priv));

	dev = (struct net_device *)
	      kmalloc(dev_per_card*sizeof(struct net_device), GFP_KERNEL);
	if (!dev) {
		printk(KERN_ERR "dscc4: can't allocate net_device\n");
		goto err_dealloc_priv;
	}
	memset(dev, 0, dev_per_card*sizeof(struct net_device));

	ppriv = (struct dscc4_pci_priv *)
		kmalloc(sizeof(struct dscc4_pci_priv), GFP_KERNEL);
	if (!ppriv) {
		printk(KERN_ERR "dscc4: can't allocate pci private data.\n");
		goto err_dealloc_dev;
	}
	memset(ppriv, 0, sizeof(struct dscc4_pci_priv));

	for (i = 0; i < dev_per_card; i++) {
		struct dscc4_dev_priv *p;
		struct net_device *d;

	        d = dev + i;
	        d->base_addr = ioaddr;
		d->init = NULL;
	        d->irq = pdev->irq;
		/* The card adds the crc */
		d->type = ARPHRD_RAWHDLC;
	        d->open = dscc4_open;
	        d->stop = dscc4_close;
	        d->hard_start_xmit = dscc4_start_xmit;
		d->set_multicast_list = NULL;
	        d->do_ioctl = dscc4_ioctl;
		d->get_stats = dscc4_get_stats;
		d->change_mtu = dscc4_change_mtu;
	        d->mtu = HDLC_MAX_MTU;
	        d->flags = IFF_MULTICAST|IFF_POINTOPOINT|IFF_NOARP;
		d->tx_timeout = dscc4_tx_timeout;
		d->watchdog_timeo = TX_TIMEOUT;

		p = dpriv + i;
		p->dev_id = i;
		p->pci_priv = ppriv;
		spin_lock_init(&p->lock);
		d->priv = p;

		if (dev_alloc_name(d, "scc%d")<0) {
			printk(KERN_ERR "dev_alloc_name failed for scc.\n");
			goto err_dealloc_dev;
		}
	        if (register_netdev(d)) {
			printk(KERN_ERR "%s: register_netdev != 0.\n", d->name);
			goto err_dealloc_dev;
	        }
		dscc4_attach_hdlc_device(d);
		SET_MODULE_OWNER(d);
	}
	ppriv->root = dev;
	ppriv->pdev = pdev;
	spin_lock_init(&ppriv->lock);
	pci_set_drvdata(pdev, ppriv);
	return 0;

err_dealloc_dev:
	while (--i >= 0)
		unregister_netdev(dev + i);
	kfree(dev);
err_dealloc_priv:
	kfree(dpriv);
err_out:
	return -1;
};

static void dscc4_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct dscc4_dev_priv *dpriv;
	struct dscc4_pci_priv *ppriv;

	dpriv = dev->priv;
	if (netif_queue_stopped(dev) && 
	   ((jiffies - dev->trans_start) > TX_TIMEOUT)) {
		ppriv = dpriv->pci_priv;
		if (dpriv->iqtx[dpriv->iqtx_current%IRQ_RING_SIZE]) {
			u32 flags;

			printk(KERN_DEBUG "%s: pending events\n", dev->name);
			dev->trans_start = jiffies;
			spin_lock_irqsave(&ppriv->lock, flags);
			dscc4_tx_irq(ppriv, dev);
			spin_unlock_irqrestore(&ppriv->lock, flags);
		} else {
			struct TxFD *tx_fd;
			struct sk_buff *skb;
			int i,j;

			printk(KERN_DEBUG "%s: missing events\n", dev->name);
			i = dpriv->tx_dirty%TX_RING_SIZE; 
			j = dpriv->tx_current - dpriv->tx_dirty;
			dpriv->stats.tx_dropped += j;
			while(j--) {
				skb = dpriv->tx_skbuff[i];
				tx_fd = dpriv->tx_fd + i;
				if (skb) {
					dpriv->tx_skbuff[i] = NULL;
					pci_unmap_single(ppriv->pdev, tx_fd->data, skb->len,
						 	 PCI_DMA_TODEVICE);
					dev_kfree_skb_irq(skb);
				} else 
					printk(KERN_INFO "%s: hardware on drugs!\n", dev->name);
				tx_fd->data = 0; /* DEBUG */
				tx_fd->complete &= ~DataComplete;
				i++;	
				i %= TX_RING_SIZE; 
			}
			dpriv->tx_dirty = dpriv->tx_current;
			dev->trans_start = jiffies;
			netif_wake_queue(dev);
			printk(KERN_DEBUG "%s: re-enabled\n", dev->name);	
		}
	}
        dpriv->timer.expires = jiffies + TX_TIMEOUT;
        add_timer(&dpriv->timer);
}

static void dscc4_tx_timeout(struct net_device *dev)
{
	/* FIXME: something is missing there */
};

static int dscc4_open(struct net_device *dev)
{
	struct dscc4_dev_priv *dpriv = (struct dscc4_dev_priv *)dev->priv;
	struct dscc4_pci_priv *ppriv;
	u32 ioaddr = 0;

	MOD_INC_USE_COUNT;

	ppriv = dpriv->pci_priv;

	if (dscc4_init_ring(dev))
		goto err_out;

	ioaddr = dev->base_addr + SCC_REG_START(dpriv->dev_id);

	/* FIXME: VIS */
	writel(readl(ioaddr + CCR0) | 0x80001000, ioaddr + CCR0);

	writel(LengthCheck | (HDLC_MAX_MRU >> 5), ioaddr + RLCR);

	/* no address recognition/crc-CCITT/cts enabled */
	writel(readl(ioaddr + CCR1) | 0x021c8000, ioaddr + CCR1);

	/* Ccr2.Rac = 0 */
	writel(0x00050008 & ~RxActivate, ioaddr + CCR2);

#ifdef EXPERIMENTAL_POLLING
	writel(0xfffeef7f, ioaddr + IMR); /* Interrupt mask */
#else
	/* Don't mask RDO. Ever. */
	//writel(0xfffaef7f, ioaddr + IMR); /* Interrupt mask */
	writel(0xfffaef7e, ioaddr + IMR); /* Interrupt mask */
#endif
	/* IDT+IDR during XPR */
	dpriv->flags = NeedIDR | NeedIDT;

	/*
	 * The following is a bit paranoid...
	 *
	 * NB: the datasheet "...CEC will stay active if the SCC is in
	 * power-down mode or..." and CCR2.RAC = 1 are two different
	 * situations.
	 */
	if (readl(ioaddr + STAR) & SccBusy) {
		printk(KERN_ERR "%s busy. Try later\n", dev->name);
		goto err_free_ring;
	}
	writel(TxSccRes | RxSccRes, ioaddr + CMDR);

	/* ... the following isn't */
	if (dscc4_wait_ack_cec(ioaddr, dev, "Cec"))
		goto err_free_ring;

	/* 
	 * I would expect XPR near CE completion (before ? after ?).
	 * At worst, this code won't see a late XPR and people
	 * will have to re-issue an ifconfig (this is harmless). 
	 * WARNING, a really missing XPR usually means a hardware 
	 * reset is needed. Suggestions anyone ?
	 */
	if (dscc4_xpr_ack(dpriv))
		goto err_free_ring;
	
	netif_start_queue(dev);

        init_timer(&dpriv->timer);
        dpriv->timer.expires = jiffies + 10*HZ;
        dpriv->timer.data = (unsigned long)dev;
        dpriv->timer.function = &dscc4_timer;
        add_timer(&dpriv->timer);
	netif_carrier_on(dev);

	return 0;

err_free_ring:
	dscc4_release_ring(dpriv);
err_out:
	MOD_DEC_USE_COUNT;
	return -EAGAIN;
}

#ifdef EXPERIMENTAL_POLLING
static int dscc4_tx_poll(struct dscc4_dev_priv *dpriv, struct net_device *dev)
{
	/* FIXME: it's gonna be easy (TM), for sure */
}
#endif /* EXPERIMENTAL_POLLING */

static int dscc4_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dscc4_dev_priv *dpriv = dev->priv;
	struct dscc4_pci_priv *ppriv;
	struct TxFD *tx_fd;
	int cur, next;

	ppriv = dpriv->pci_priv;
	cur = dpriv->tx_current++%TX_RING_SIZE;
	next = dpriv->tx_current%TX_RING_SIZE;
	dpriv->tx_skbuff[next] = skb;
	tx_fd = dpriv->tx_fd + next;
	tx_fd->state = FrameEnd | Hold | TO_STATE(skb->len & TxSizeMax);
	tx_fd->data = pci_map_single(ppriv->pdev, skb->data, skb->len,
				     PCI_DMA_TODEVICE);
	tx_fd->complete = 0x00000000;
	mb(); // FIXME: suppress ?

#ifdef EXPERIMENTAL_POLLING
	spin_lock(&dpriv->lock);
	while(dscc4_tx_poll(dpriv, dev));
	spin_unlock(&dpriv->lock);
#endif
	/*
	 * I know there's a window for a race in the following lines but
	 * dscc4_timer will take good care of it. The chipset eats events
	 * (especially the net_dev re-enabling ones) thus there is no
	 * reason to try and be smart.
	 */
	if ((dpriv->tx_dirty + 16) < dpriv->tx_current) {
			netif_stop_queue(dev);
			dpriv->hi_expected = 2;
	}
	tx_fd = dpriv->tx_fd + cur;
	tx_fd->state &= ~Hold;
	mb(); // FIXME: suppress ?

	/* 
	 * One may avoid some pci transactions during intense TX periods.
	 * Not sure it's worth the pain...
	 */
	writel((TxPollCmd << dpriv->dev_id) | NoAck, dev->base_addr + GCMDR);
	dev->trans_start = jiffies;
	return 0;
}

static int dscc4_close(struct net_device *dev)
{
	struct dscc4_dev_priv *dpriv = (struct dscc4_dev_priv *)dev->priv;
	u32 ioaddr = dev->base_addr;
	int dev_id;

	del_timer_sync(&dpriv->timer);
	netif_stop_queue(dev);

	dev_id = dpriv->dev_id;

	writel(0x00050000, ioaddr + SCC_REG_START(dev_id) + CCR2);
	writel(MTFi|Rdr|Rdt, ioaddr + CH0CFG + dev_id*0x0c); /* Reset Rx/Tx */
	writel(0x00000001, ioaddr + GCMDR);

	dscc4_release_ring(dpriv);

	MOD_DEC_USE_COUNT;
	return 0;
}

static int dscc4_set_clock(struct net_device *dev, u32 *bps, u32 *state)
{
	struct dscc4_dev_priv *dpriv = (struct dscc4_dev_priv *)dev->priv;
	u32 brr;

	*state &= ~Ccr0ClockMask;
	if (*bps) { /* DCE */
		u32 n = 0, m = 0, divider;
		int xtal;

		xtal = dpriv->pci_priv->xtal_hz;
		if (!xtal)
			return -1;
		divider = xtal / *bps;
		if (divider > BRR_DIVIDER_MAX) {
			divider >>= 4;
			*state |= 0x00000036; /* Clock mode 6b (BRG/16) */
		} else
			*state |= 0x00000037; /* Clock mode 7b (BRG) */
		if (divider >> 22) {
			n = 63;
			m = 15;
		} else if (divider) {
			/* Extraction of the 6 highest weighted bits */
			m = 0;
			while (0xffffffc0 & divider) {
				m++;
				divider >>= 1;
			}
			n = divider;
		}
		brr = (m << 8) | n;
		divider = n << m;
		if (!(*state & 0x00000001)) /* Clock mode 6b */
			divider <<= 4;
		*bps = xtal / divider;
	} else { /* DTE */
		/* 
		 * "state" already reflects Clock mode 0a. 
		 * Nothing more to be done 
		 */
		brr = 0;
	}
	writel(brr, dev->base_addr + BRR + SCC_REG_START(dpriv->dev_id));

	return 0;
}

#ifdef LATER_PLEASE
/*
 * -*- [RFC] Configuring Synchronous Interfaces in Linux -*-
 */

// FIXME: MEDIA already defined in linux/hdlc.h
#define HDLC_MEDIA_V35		0
#define HDLC_MEDIA_RS232	1
#define HDLC_MEDIA_X21		2
#define HDLC_MEDIA_E1		3
#define HDLC_MEDIA_HSSI		4

#define HDLC_CODING_NRZ		0
#define HDLC_CODING_NRZI	1
#define HDLC_CODING_FM0		2
#define HDLC_CODING_FM1		3
#define HDLC_CODING_MANCHESTER	4

#define HDLC_CRC_NONE		0
#define HDLC_CRC_16		1
#define HDLC_CRC_32		2
#define HDLC_CRC_CCITT		3

/* RFC: add the crc reset value ? */
struct hdlc_physical {
	u8 media;
	u8 coding;
	u32 rate;
	u8 crc;
	u8 crc_siz;		/* 2 or 4 bytes */
	u8 shared_flags;	/* Discouraged on the DSCC4 */
};

// FIXME: PROTO already defined in linux/hdlc.h
#define HDLC_PROTO_RAW		0
#define HDLC_PROTO_FR		1
#define HDLC_PROTO_X25		2
#define HDLC_PROTO_PPP		3
#define HDLC_PROTO_CHDLC	4

struct hdlc_protocol {
	u8 proto; 
	
	union {
	} u;
};

struct screq {
	u16 media_group;

	union {
		struct hdlc_physical hdlc_phy;
		struct hdlc_protocol hdlc_proto;
	} u;
};

// FIXME: go sub-module 
static struct {
	u16 coding;
	u16 bits;
} map[] = {
	{HDLC_CODING_NRZ,		0x00},
	{HDLC_CODING_NRZI,		0x20},
	{HDLC_CODING_FM0,		0x40},
	{HDLC_CODING_FM1,		0x50},
	{HDLC_CODING_MANCHESTER,	0x60},
	{65535,	0x00}
};
#endif /* LATER_PLEASE */

static int dscc4_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct dscc4_dev_priv *dpriv = dev->priv;
	u32 state, ioaddr;

        if (dev->flags & IFF_UP)
                return -EBUSY;

	switch (cmd) {
		/* Set built-in quartz frequency */
		case SIOCDEVPRIVATE: {
			u32 hz;

			hz = ifr->ifr_ifru.ifru_ivalue;
			if (hz >= 33000000) /* 33 MHz */
				return -EOPNOTSUPP;
			dpriv->pci_priv->xtal_hz = hz;
			return 0;
		}
		/* Set/unset loopback */
		case SIOCDEVPRIVATE+1: {
			u32 flags;
	
			ioaddr = dev->base_addr + CCR1 +
				 SCC_REG_START(dpriv->dev_id);
			state = readl(ioaddr);
			flags = ifr->ifr_ifru.ifru_ivalue;
			if (flags & 0x00000001) {
				printk(KERN_DEBUG "%s: loopback\n", dev->name);
				state |= 0x00000100;
			} else {
				printk(KERN_DEBUG "%s: normal\n", dev->name);
				state &= ~0x00000100;
			}
			writel(state, ioaddr);
			return 0;
		}

#ifdef LATER_PLEASE
		case SIOCDEVPRIVATE+2: {
		{
			struct screq scr;

			err = copy_from_user(&scr, ifr->ifr_ifru.ifru_data, sizeof(struct screq));
			if (err)
				return err;
			do {
				if (scr.u.hdlc_phy.coding == map[i].coding)
					break;
			} while (map[++i].coding != 65535);
			if (!map[i].coding)
				return -EOPNOTSUPP;
			
			ioaddr = dev->base_addr + CCR0 +
				 SCC_REG_START(dpriv->dev_id);
			state = readl(ioaddr) & ~EncodingMask;
			state |= (u32)map[i].bits << 16;
			writel(state, ioaddr);	
			printk("state: %08x\n", state); /* DEBUG */
			return 0;
		}
		case SIOCDEVPRIVATE+3: {
			struct screq *scr = (struct screq *)ifr->ifr_ifru.ifru_data;

			ioaddr = dev->base_addr + CCR0 +
				 SCC_REG_START(dpriv->dev_id);
			state = (readl(ioaddr) & EncodingMask) >> 16;
			do {
				if (state == map[i].bits)
					break;
			} while (map[++i].coding);
			return put_user(map[i].coding, (u16 *)scr->u.hdlc_phy.coding);
		}
#endif /* LATER_PLEASE */

		case HDLCSCLOCKRATE:
		{
			u32 state, bps;
 
			bps = ifr->ifr_ifru.ifru_ivalue;
			ioaddr = dev->base_addr + CCR0 +
				 SCC_REG_START(dpriv->dev_id);
			state = readl(ioaddr);
			if(dscc4_set_clock(dev, &bps, &state) < 0)
				return -EOPNOTSUPP;
			if (bps) { /* DCE */
				printk(KERN_DEBUG "%s: generated RxClk (DCE)\n",
				       dev->name);
				ifr->ifr_ifru.ifru_ivalue = bps;
			} else { /* DTE */
				state = 0x80001000;
				printk(KERN_DEBUG "%s: external RxClk (DTE)\n",
				       dev->name);
			}
			writel(state, ioaddr);	
			return 0;
		}
		case HDLCGCLOCKRATE: {
			u32 brr;
			int bps;

			brr = readl(dev->base_addr + BRR + 
				    SCC_REG_START(dpriv->dev_id));
			bps = dpriv->pci_priv->xtal_hz >> (brr >> 8);
			bps /= (brr & 0x3f) + 1;
			ifr->ifr_ifru.ifru_ivalue = bps;
			return 0;
		}

		default:
			return -EOPNOTSUPP;
	}
}

static int dscc4_change_mtu(struct net_device *dev, int mtu)
{
	/* FIXME: chainsaw coded... */
	if ((mtu <= 3) || (mtu > 65531))
		return -EINVAL;
        if(dev->flags & IFF_UP)
                return -EBUSY;
	dev->mtu = mtu;
        return(0);
}

static void dscc4_irq(int irq, void *dev_instance, struct pt_regs *ptregs)
{
	struct net_device *dev = dev_instance;
	struct dscc4_pci_priv *priv;
	u32 ioaddr, state;
	unsigned long flags;
	int i;

	priv = ((struct dscc4_dev_priv *)dev->priv)->pci_priv;
	/* 
	 * FIXME: shorten the protected area (set some bit telling we're
	 * in an interrupt or increment some work-to-do counter etc...)
	 */
	spin_lock_irqsave(&priv->lock, flags);

	ioaddr = dev->base_addr;

	state = readl(ioaddr + GSTAR);
	if (!state)
		goto out;
	writel(state, ioaddr + GSTAR);

	if (state & Arf) { 
		printk(KERN_ERR "%s: failure (Arf). Harass the maintener\n",
		       dev->name);
		goto out;
	}
	state &= ~ArAck;
	if (state & Cfg) {
		if (debug)
			printk(KERN_DEBUG "CfgIV\n");
		if (priv->iqcfg[priv->cfg_cur++%IRQ_RING_SIZE] & Arf)
			printk(KERN_ERR "%s: %s failed\n", dev->name, "CFG");
		if (!(state &= ~Cfg))
			goto out;
	}
	if (state & RxEvt) {
		i = dev_per_card - 1;
		do {
			dscc4_rx_irq(priv, dev + i);
		} while (--i >= 0);
		state &= ~RxEvt;
	}
	if (state & TxEvt) {
		i = dev_per_card - 1;
		do {
			dscc4_tx_irq(priv, dev + i);
		} while (--i >= 0);
		state &= ~TxEvt;
	}
out:
	spin_unlock_irqrestore(&priv->lock, flags);
}

static __inline__ void dscc4_tx_irq(struct dscc4_pci_priv *ppriv, 
				    struct net_device *dev)
{
	struct dscc4_dev_priv *dpriv = dev->priv;
	u32 state;
	int cur, loop = 0;

try:
	cur = dpriv->iqtx_current%IRQ_RING_SIZE;
	state = dpriv->iqtx[cur];
	if (!state) {
#ifdef DEBUG
		if (loop > 1)
			printk(KERN_DEBUG "%s: Tx irq loop=%d\n", dev->name, loop);
#endif
		if (loop && netif_queue_stopped(dev))
			if ((dpriv->tx_dirty + 8) >= dpriv->tx_current)
				netif_wake_queue(dev);
		return;
	}
	loop++;
	dpriv->iqtx[cur] = 0;
	dpriv->iqtx_current++;

#ifdef DEBUG_PARANOID
	if (SOURCE_ID(state) != dpriv->dev_id) {
		printk(KERN_DEBUG "%s (Tx): Source Id=%d, state=%08x\n",
		       dev->name, SOURCE_ID(state), state );
		return;
	}
	if (state & 0x0df80c00) {
		printk(KERN_DEBUG "%s (Tx): state=%08x (UFO alert)\n",
		       dev->name, state);
		return;
	}
#endif
	// state &= 0x0fffffff; /* Tracking the analyzed bits */
	if (state & SccEvt) {
		if (state & Alls) {
			struct TxFD *tx_fd;
			struct sk_buff *skb;

			cur = dpriv->tx_dirty%TX_RING_SIZE;
			tx_fd = dpriv->tx_fd + cur;

			skb = dpriv->tx_skbuff[cur];

			/* XXX: hideous kludge - to be removed "later" */
			if (!skb) {
				printk(KERN_ERR "%s: NULL skb in tx_irq at index %d\n", dev->name, cur);
				goto try;
			}
			dpriv->tx_dirty++; // MUST be after skb test

			/* Happens sometime. Don't know what triggers it */
			if (!(tx_fd->complete & DataComplete)) {
				u32 ioaddr, isr;

				ioaddr = dev->base_addr + 
					 SCC_REG_START(dpriv->dev_id) + ISR;
				isr = readl(ioaddr);
				printk(KERN_DEBUG 
				       "%s: DataComplete=0 cur=%d isr=%08x state=%08x\n",
				       dev->name, cur, isr, state);
				writel(isr, ioaddr);
				dpriv->stats.tx_dropped++;
			} else {
				tx_fd->complete &= ~DataComplete;
				if (tx_fd->state & FrameEnd) {
					dpriv->stats.tx_packets++;
					dpriv->stats.tx_bytes += skb->len;
				}
			}

			dpriv->tx_skbuff[cur] = NULL;
			pci_unmap_single(ppriv->pdev, tx_fd->data, skb->len,
					 PCI_DMA_TODEVICE);
			tx_fd->data = 0; /* DEBUG */
			dev_kfree_skb_irq(skb);
{ // DEBUG
			cur = (dpriv->tx_dirty-1)%TX_RING_SIZE;
			tx_fd = dpriv->tx_fd + cur;
			tx_fd->state |= Hold;
}
			if (!(state &= ~Alls))
				goto try;
		}
		/* 
		 * Transmit Data Underrun
		 */
		if (state & Xdu) {
			printk(KERN_ERR "dscc4: XDU. Contact maintainer\n");
			dpriv->flags = NeedIDT; 
			/* Tx reset */
			writel(MTFi | Rdt, 
			       dev->base_addr + 0x0c*dpriv->dev_id + CH0CFG);
			writel(0x00000001, dev->base_addr + GCMDR);
			return;
		}
		if (state & Xmr) {
			/* Frame needs to be sent again - FIXME */
			//dscc4_start_xmit(dpriv->tx_skbuff[dpriv->tx_dirty], dev);
			if (!(state &= ~0x00002000)) /* DEBUG */
				goto try;
		}
		if (state & Xpr) {
			unsigned long ioaddr = dev->base_addr;
			unsigned long scc_offset;
			u32 scc_addr;

			scc_offset = ioaddr + SCC_REG_START(dpriv->dev_id);
			scc_addr = ioaddr + 0x0c*dpriv->dev_id;
			if (readl(scc_offset + STAR) & SccBusy)
				printk(KERN_DEBUG "%s busy. Fatal\n", 
				       dev->name);
			/*
			 * Keep this order: IDT before IDR
			 */
			if (dpriv->flags & NeedIDT) {
				writel(MTFi | Idt, scc_addr + CH0CFG);
				writel(dpriv->tx_fd_dma + 
				       (dpriv->tx_dirty%TX_RING_SIZE)*
				       sizeof(struct TxFD), scc_addr + CH0BTDA);
				if(dscc4_do_action(dev, "IDT"))
					goto err_xpr;
				dpriv->flags &= ~NeedIDT;
				mb();
			}
			if (dpriv->flags & NeedIDR) {
				writel(MTFi | Idr, scc_addr + CH0CFG);
				writel(dpriv->rx_fd_dma + 
				       (dpriv->rx_current%RX_RING_SIZE)*
				       sizeof(struct RxFD), scc_addr + CH0BRDA);
				if(dscc4_do_action(dev, "IDR"))
					goto err_xpr;
				dpriv->flags &= ~NeedIDR;
				mb();
				/* Activate receiver and misc */
				writel(0x08050008, scc_offset + CCR2);
			}
		err_xpr:
			if (!(state &= ~Xpr))
				goto try;
		}
	} else { /* ! SccEvt */
		if (state & Hi) {
#ifdef EXPERIMENTAL_POLLING
			while(!dscc4_tx_poll(dpriv, dev));
#endif
			state &= ~Hi;
		}
		/*
		 * FIXME: it may be avoided. Re-re-re-read the manual.
		 */
		if (state & Err) {
			printk(KERN_ERR "%s: Tx ERR\n", dev->name);
			dpriv->stats.tx_errors++;
			state &= ~Err;
		}
	}
	goto try;
}

static __inline__ void dscc4_rx_irq(struct dscc4_pci_priv *priv, struct net_device *dev)
{
	struct dscc4_dev_priv *dpriv = dev->priv;
	u32 state;
	int cur;

try:
	cur = dpriv->iqrx_current%IRQ_RING_SIZE;
	state = dpriv->iqrx[cur];
	if (!state)
		return;
	dpriv->iqrx[cur] = 0;
	dpriv->iqrx_current++;

#ifdef DEBUG_PARANOID
	if (SOURCE_ID(state) != dpriv->dev_id) {
		printk(KERN_DEBUG "%s (Rx): Source Id=%d, state=%08x\n",
		       dev->name, SOURCE_ID(state), state);
		goto try;
	}
	if (state & 0x0df80c00) {
		printk(KERN_DEBUG "%s (Rx): state=%08x (UFO alert)\n",
		       dev->name, state);
		goto try;
	}
#endif
	if (!(state & SccEvt)){
		struct RxFD *rx_fd;

		state &= 0x00ffffff;
		if (state & Err) { /* Hold or reset */
			printk(KERN_DEBUG "%s (Rx): ERR\n", dev->name);
			cur = dpriv->rx_current;
			rx_fd = dpriv->rx_fd + cur;
			/*
			 * Presume we're not facing a DMAC receiver reset. 
			 * As We use the rx size-filtering feature of the 
			 * DSCC4, the beginning of a new frame is waiting in 
			 * the rx fifo. I bet a Receive Data Overflow will 
			 * happen most of time but let's try and avoid it.
			 * Btw (as for RDO) if one experiences ERR whereas
			 * the system looks rather idle, there may be a 
			 * problem with latency. In this case, increasing
			 * RX_RING_SIZE may help.
			 */
			while (dpriv->rx_needs_refill) {
				while(!(rx_fd->state1 & Hold)) {
					rx_fd++;
					cur++;
					if (!(cur = cur%RX_RING_SIZE))
						rx_fd = dpriv->rx_fd;
				}
				dpriv->rx_needs_refill--;
				try_get_rx_skb(dpriv, cur, dev);
				if (!rx_fd->data)
					goto try;
				rx_fd->state1 &= ~Hold;
				rx_fd->state2 = 0x00000000;
				rx_fd->end = 0xbabeface;
			}
			goto try;
		}
		if (state & Fi) {
			cur = dpriv->rx_current%RX_RING_SIZE;
			rx_fd = dpriv->rx_fd + cur;
			dscc4_rx_skb(dpriv, cur, rx_fd, dev);
			dpriv->rx_current++;
			goto try;
		}
		if (state & Hi ) { /* HI bit */
			state &= ~Hi;
			goto try;
		}
	} else { /* ! SccEvt */
#ifdef DEBUG_PARANOIA
		int i;
		static struct {
			u32 mask;
			const char *irq_name;
		} evts[] = {
			{ 0x00008000, "TIN"},
			{ 0x00004000, "CSC"},
			{ 0x00000020, "RSC"},
			{ 0x00000010, "PCE"},
			{ 0x00000008, "PLLA"},
			{ 0x00000004, "CDSC"},
			{ 0, NULL}
		};
#endif /* DEBUG_PARANOIA */
		state &= 0x00ffffff;
#ifdef DEBUG_PARANOIA
		for (i = 0; evts[i].irq_name; i++) {
			if (state & evts[i].mask) {
				printk(KERN_DEBUG "dscc4(%s): %s\n",
					dev->name, evts[i].irq_name);
				if (!(state &= ~evts[i].mask))
					goto try;
			}
		}
#endif /* DEBUG_PARANOIA */
		/*
		 * Receive Data Overflow (FIXME: untested)
		 */
		if (state & Rdo) {
			u32 ioaddr, scc_offset, scc_addr;
			struct RxFD *rx_fd;
			int cur;

			//if (debug)
			//	dscc4_rx_dump(dpriv);
			ioaddr = dev->base_addr;
			scc_addr = ioaddr + 0x0c*dpriv->dev_id;
			scc_offset = ioaddr + SCC_REG_START(dpriv->dev_id);

			writel(readl(scc_offset + CCR2) & ~RxActivate, 
			       scc_offset + CCR2);
			/*
			 * This has no effect. Why ?
			 * ORed with TxSccRes, one sees the CFG ack (for
			 * the TX part only).
			 */	
			writel(RxSccRes, scc_offset + CMDR);
			dpriv->flags |= RdoSet;

			/* 
			 * Let's try and save something in the received data.
			 * rx_current must be incremented at least once to
			 * avoid HOLD in the BRDA-to-be-pointed desc.
			 */
			do {
				cur = dpriv->rx_current++%RX_RING_SIZE;
				rx_fd = dpriv->rx_fd + cur;
				if (!(rx_fd->state2 & DataComplete))
					break;
				if (rx_fd->state2 & FrameAborted) {
					dpriv->stats.rx_over_errors++;
					rx_fd->state1 |= Hold;
					rx_fd->state2 = 0x00000000;
					rx_fd->end = 0xbabeface;
				} else 
					dscc4_rx_skb(dpriv, cur, rx_fd, dev);
			} while (1);

			if (debug) {
				if (dpriv->flags & RdoSet)
					printk(KERN_DEBUG 
					       "dscc4: no RDO in Rx data\n");
			}
#ifdef DSCC4_RDO_EXPERIMENTAL_RECOVERY
			/*
			 * FIXME: must the reset be this violent ?
			 */
			writel(dpriv->rx_fd_dma + 
			       (dpriv->rx_current%RX_RING_SIZE)*
			       sizeof(struct RxFD), scc_addr + CH0BRDA);
			writel(MTFi|Rdr|Idr, scc_addr + CH0CFG);
			if(dscc4_do_action(dev, "RDR")) {
				printk(KERN_ERR "%s: RDO recovery failed(%s)\n",
				       dev->name, "RDR");
				goto rdo_end;
			}
			writel(MTFi|Idr, scc_addr + CH0CFG);
			if(dscc4_do_action(dev, "IDR")) {
				printk(KERN_ERR "%s: RDO recovery failed(%s)\n",
				       dev->name, "IDR");
				goto rdo_end;
			}
		rdo_end:
#endif
			writel(readl(scc_offset + CCR2) | RxActivate, 
			       scc_offset + CCR2);
			goto try;
		}
		/* These will be used later */
		if (state & Rfs) {
			if (!(state &= ~Rfs))
				goto try;
		}
		if (state & Rfo) {
			if (!(state &= ~Rfo))
				goto try;
		}
		if (state & Flex) {
			if (!(state &= ~Flex))
				goto try;
		}
	}
}

static int dscc4_init_ring(struct net_device *dev)
{
	struct dscc4_dev_priv *dpriv = (struct dscc4_dev_priv *)dev->priv;
	struct TxFD *tx_fd;
	struct RxFD *rx_fd;
	int i;

	tx_fd = (struct TxFD *) pci_alloc_consistent(dpriv->pci_priv->pdev,
		TX_RING_SIZE*sizeof(struct TxFD), &dpriv->tx_fd_dma);
	if (!tx_fd)
		goto err_out;
	rx_fd = (struct RxFD *) pci_alloc_consistent(dpriv->pci_priv->pdev,
		RX_RING_SIZE*sizeof(struct RxFD), &dpriv->rx_fd_dma);
	if (!rx_fd)
		goto err_free_dma_tx;

	dpriv->tx_fd = tx_fd;
	dpriv->rx_fd = rx_fd;
	dpriv->rx_current = 0;
	dpriv->tx_current = 0;
	dpriv->tx_dirty = 0;

	/* the dma core of the dscc4 will be locked on the first desc */
	for(i = 0; i < TX_RING_SIZE; ) {
		reset_TxFD(tx_fd);
	        /* FIXME: NULL should be ok - to be tried */
	        tx_fd->data = dpriv->tx_fd_dma;
	        dpriv->tx_skbuff[i] = NULL;
		i++;
		tx_fd->next = (u32)(dpriv->tx_fd_dma + i*sizeof(struct TxFD));
		tx_fd++;
	}
	(--tx_fd)->next = (u32)dpriv->tx_fd_dma;
{
	/*
	 * XXX: I would expect the following to work for the first descriptor
	 * (tx_fd->state = 0xc0000000)
	 * - Hold=1 (don't try and branch to the next descripto);
	 * - No=0 (I want an empty data section, i.e. size=0);
	 * - Fe=1 (required by No=0 or we got an Err irq and must reset).
	 * Alas, it fails (and locks solid). Thus the introduction of a dummy
	 * skb to avoid No=0 (choose one: Ugly [ ] Tasteless [ ] VMS [ ]).
	 * TODO: fiddle the tx threshold when time permits.
	 */
	struct sk_buff *skb;

	skb = dev_alloc_skb(32);
	if (!skb)
		goto err_free_dma_tx;
	skb->len = 32;
	memset(skb->data, 0xaa, 16);
	tx_fd -= (TX_RING_SIZE - 1);
	tx_fd->state = 0xc0000000;
	tx_fd->state |= ((u32)(skb->len & TxSizeMax)) << 16;
	tx_fd->data = pci_map_single(dpriv->pci_priv->pdev, skb->data, 
				     skb->len, PCI_DMA_TODEVICE);
	dpriv->tx_skbuff[0] = skb;
}
	for (i = 0; i < RX_RING_SIZE;) {
		/* size set by the host. Multiple of 4 bytes please */
	        rx_fd->state1 = HiDesc; /* Hi, no Hold */
	        rx_fd->state2 = 0x00000000;
	        rx_fd->end = 0xbabeface;
	        rx_fd->state1 |= ((u32)(HDLC_MAX_MRU & RxSizeMax)) << 16;
		try_get_rx_skb(dpriv, i, dev);
		i++;
		rx_fd->next = (u32)(dpriv->rx_fd_dma + i*sizeof(struct RxFD));
		rx_fd++;
	}
	(--rx_fd)->next = (u32)dpriv->rx_fd_dma;
	rx_fd->state1 |= 0x40000000; /* Hold */

	return 0;

err_free_dma_tx:
	pci_free_consistent(dpriv->pci_priv->pdev, TX_RING_SIZE*sizeof(*tx_fd),
			    tx_fd, dpriv->tx_fd_dma);
err_out:
	return -1;
}

static struct net_device_stats *dscc4_get_stats(struct net_device *dev)
{
	struct dscc4_dev_priv *priv = (struct dscc4_dev_priv *)dev->priv;

	return &priv->stats;
}

static void __exit dscc4_remove_one(struct pci_dev *pdev)
{
	struct dscc4_pci_priv *ppriv;
	struct net_device *root;
	int i;

	ppriv = pci_get_drvdata(pdev);
	root = ppriv->root;

	free_irq(pdev->irq, root);
	pci_free_consistent(pdev, IRQ_RING_SIZE*sizeof(u32), ppriv->iqcfg, 
			    ppriv->iqcfg_dma);
	for (i=0; i < dev_per_card; i++) {
		struct dscc4_dev_priv *dpriv;
		struct net_device *dev;

		dev = ppriv->root + i;
		dscc4_unattach_hdlc_device(dev);

		dpriv = (struct dscc4_dev_priv *)dev->priv;
		pci_free_consistent(pdev, IRQ_RING_SIZE*sizeof(u32), 
				    dpriv->iqrx, dpriv->iqrx_dma);
		pci_free_consistent(pdev, IRQ_RING_SIZE*sizeof(u32), 
				    dpriv->iqtx, dpriv->iqtx_dma);
		unregister_netdev(dev);
	}
	kfree(root->priv);

	iounmap((void *)root->base_addr);
	kfree(root);

	kfree(ppriv);

	release_mem_region(pci_resource_start(pdev, 1),
			   pci_resource_len(pdev, 1));
	release_mem_region(pci_resource_start(pdev, 0),
			   pci_resource_len(pdev, 0));
}

static int dscc4_hdlc_ioctl(struct hdlc_device_struct *hdlc, struct ifreq *ifr, int cmd)
{
	struct net_device *dev = (struct net_device *)hdlc->netdev.base_addr;
	int result;

	/* FIXME: locking ? */
	result = dscc4_ioctl(dev, ifr, cmd);
	return result;
}

static int dscc4_hdlc_open(struct hdlc_device_struct *hdlc)
{
	struct net_device *dev = (struct net_device *)(hdlc->netdev.base_addr);

        if (netif_running(dev)) {
		printk(KERN_DEBUG "%s: already running\n", dev->name); // DEBUG
		return 0;
	}
	return dscc4_open(dev);
}

static int dscc4_hdlc_xmit(hdlc_device *hdlc, struct sk_buff *skb)
{
	struct net_device *dev = (struct net_device *)hdlc->netdev.base_addr;

	return dscc4_start_xmit(skb, dev);
}

static void dscc4_hdlc_close(struct hdlc_device_struct *hdlc)
{
	struct net_device *dev = (struct net_device *)hdlc->netdev.base_addr;
	struct dscc4_dev_priv *dpriv;

	dpriv = dev->priv;
	--dpriv->usecount;
}

/* Operated under dev lock */
static int dscc4_attach_hdlc_device(struct net_device *dev) 
{
	struct dscc4_dev_priv *dpriv = dev->priv;
	struct hdlc_device_struct *hdlc;
	int result;

	hdlc = &dpriv->hdlc;
	/* XXX: Don't look at the next line */
	hdlc->netdev.base_addr = (unsigned long)dev;
	hdlc->set_mode = NULL;
	hdlc->open = dscc4_hdlc_open;
	hdlc->close = dscc4_hdlc_close;
	hdlc->ioctl = dscc4_hdlc_ioctl;
	hdlc->xmit = dscc4_hdlc_xmit;

	result = register_hdlc_device(hdlc);
	if (!result)
		dpriv->usecount++;
	return result;
}

/* Operated under dev lock */
static void dscc4_unattach_hdlc_device(struct net_device *dev) 
{
	struct dscc4_dev_priv *dpriv = dev->priv;

	unregister_hdlc_device(&dpriv->hdlc);
	dpriv->usecount--;
}

static struct pci_device_id dscc4_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_SIEMENS, PCI_DEVICE_ID_SIEMENS_DSCC4,
	        PCI_ANY_ID, PCI_ANY_ID, },
	{ 0,}
};
MODULE_DEVICE_TABLE(pci, dscc4_pci_tbl);

static struct pci_driver dscc4_driver = {
	name:           "dscc4",
	id_table:       dscc4_pci_tbl,
	probe:          dscc4_init_one,
	remove:         dscc4_remove_one,
};

static int __init dscc4_init_module(void)
{
	return pci_module_init(&dscc4_driver);
}

static void __exit dscc4_cleanup_module(void)
{
	pci_unregister_driver(&dscc4_driver);
}

module_init(dscc4_init_module);
module_exit(dscc4_cleanup_module);
