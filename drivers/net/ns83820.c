#define VERSION "0.13"
/* ns83820.c by Benjamin LaHaise <bcrl@redhat.com>
 *
 * $Revision: 1.34.2.8 $
 *
 * Copyright 2001 Benjamin LaHaise.
 * Copyright 2001 Red Hat.
 *
 * Mmmm, chocolate vanilla mocha...
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * ChangeLog
 * =========
 *	20010414	0.1 - created
 *	20010622	0.2 - basic rx and tx.
 *	20010711	0.3 - added duplex and link state detection support.
 *	20010713	0.4 - zero copy, no hangs.
 *			0.5 - 64 bit dma support (davem will hate me for this)
 *			    - disable jumbo frames to avoid tx hangs
 *			    - work around tx deadlocks on my 1.02 card via
 *			      fiddling with TXCFG
 *	20010810	0.6 - use pci dma api for ringbuffers, work on ia64
 *	20010816	0.7 - misc cleanups
 *	20010826	0.8 - fix critical zero copy bugs
 *			0.9 - internal experiment
 *	20010827	0.10 - fix ia64 unaligned access.
 *	20010906	0.11 - accept all packets with checksum errors as
 *			       otherwise fragments get lost
 *			     - fix >> 32 bugs
 *			0.12 - add statistics counters
 *			     - add allmulti/promisc support
 *	20011009	0.13 - hotplug support, other smaller pci api cleanups
 *
 * Driver Overview
 * ===============
 *
 * This driver was originally written for the National Semiconductor
 * 83820 chip, a 10/100/1000 Mbps 64 bit PCI ethernet NIC.  Hopefully
 * this code will turn out to be a) clean, b) correct, and c) fast.
 * With that in mind, I'm aiming to split the code up as much as
 * reasonably possible.  At present there are X major sections that
 * break down into a) packet receive, b) packet transmit, c) link
 * management, d) initialization and configuration.  Where possible,
 * these code paths are designed to run in parallel.
 *
 * This driver has been tested and found to work with the following
 * cards (in no particular order):
 *
 *	Cameo		SOHO-GA2000T	SOHO-GA2500T
 *	D-Link		DGE-500T
 *	PureData	PDP8023Z-TG
 *	SMC		SMC9452TX	SMC9462TX
 *
 * Special thanks to SMC for providing hardware to test this driver on.
 *
 * Reports of success or failure would be greatly appreciated.
 */
//#define dprintk		printk
#define dprintk(x...)		do { } while (0)

#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/smp_lock.h>
#include <linux/tqueue.h>
#include <linux/init.h>
#include <linux/ip.h>	/* for iph */
#include <linux/in.h>	/* for IPPROTO_... */
#include <linux/eeprom.h>
#include <linux/compiler.h>
//#include <linux/skbrefill.h>

#include <asm/io.h>

/* Dprintk is used for more interesting debug events */
#undef Dprintk
#define	Dprintk			dprintk

#ifdef CONFIG_HIGHMEM64G
#define USE_64BIT_ADDR
#elif defined(__ia64__)
#define USE_64BIT_ADDR
#endif

/* Tell davem to fix the pci dma api.  Grrr. */
/* stolen from acenic.c */
#ifdef CONFIG_HIGHMEM
#if defined(CONFIG_X86)
#define DMAADDR_OFFSET  0
#if defined(CONFIG_HIGHMEM64G)
typedef u64 dmaaddr_high_t;
#else
typedef u32 dmaaddr_high_t;
#endif
#elif defined(CONFIG_PPC)
#define DMAADDR_OFFSET PCI_DRAM_OFFSET
typedef unsigned long dmaaddr_high_t;
#endif

static inline dmaaddr_high_t
pci_map_single_high(struct pci_dev *hwdev, struct page *page,
		    int offset, size_t size, int dir)
{
	u64 phys;
	phys = page - mem_map;
	phys <<= PAGE_SHIFT;
	phys += offset;
	phys += DMAADDR_OFFSET;
	return phys;
}
#else

typedef unsigned long dmaaddr_high_t;

static inline dmaaddr_high_t
pci_map_single_high(struct pci_dev *hwdev, struct page *page,
		    int offset, size_t size, int dir)
{
	return pci_map_single(hwdev, page_address(page) + offset, size, dir);
}
#endif

/* tunables */
#define RX_BUF_SIZE	6144	/* 8192 */
#define NR_RX_DESC	256

#define NR_TX_DESC	256

/* register defines */
#define CFGCS		0x04

#define CR_TXE		0x00000001
#define CR_TXD		0x00000002
#define CR_RXE		0x00000004
#define CR_RXD		0x00000008
#define CR_TXR		0x00000010
#define CR_RXR		0x00000020
#define CR_SWI		0x00000080
#define CR_RST		0x00000100

#define PTSCR_EEBIST_EN	0x00000002
#define PTSCR_EELOAD_EN	0x00000004

#define ISR_TXDESC3	0x40000000
#define ISR_TXDESC2	0x20000000
#define ISR_TXDESC1	0x10000000
#define ISR_TXDESC0	0x08000000
#define ISR_RXDESC3	0x04000000
#define ISR_RXDESC2	0x02000000
#define ISR_RXDESC1	0x01000000
#define ISR_RXDESC0	0x00800000
#define ISR_TXRCMP	0x00400000
#define ISR_RXRCMP	0x00200000
#define ISR_DPERR	0x00100000
#define ISR_SSERR	0x00080000
#define ISR_RMABT	0x00040000
#define ISR_RTABT	0x00020000
#define ISR_RXSOVR	0x00010000
#define ISR_HIBINT	0x00008000
#define ISR_PHY		0x00004000
#define ISR_PME		0x00002000
#define ISR_SWI		0x00001000
#define ISR_MIB		0x00000800
#define ISR_TXURN	0x00000400
#define ISR_TXIDLE	0x00000200
#define ISR_TXERR	0x00000100
#define ISR_TXDESC	0x00000080
#define ISR_TXOK	0x00000040
#define ISR_RXORN	0x00000020
#define ISR_RXIDLE	0x00000010
#define ISR_RXEARLY	0x00000008
#define ISR_RXERR	0x00000004
#define ISR_RXDESC	0x00000002
#define ISR_RXOK	0x00000001

#define TXCFG_CSI	0x80000000
#define TXCFG_HBI	0x40000000
#define TXCFG_MLB	0x20000000
#define TXCFG_ATP	0x10000000
#define TXCFG_ECRETRY	0x00800000
#define TXCFG_BRST_DIS	0x00080000
#define TXCFG_MXDMA1024	0x00000000
#define TXCFG_MXDMA512	0x00700000
#define TXCFG_MXDMA256	0x00600000
#define TXCFG_MXDMA128	0x00500000
#define TXCFG_MXDMA64	0x00400000
#define TXCFG_MXDMA32	0x00300000
#define TXCFG_MXDMA16	0x00200000
#define TXCFG_MXDMA8	0x00100000

#define CFG_LNKSTS	0x80000000
#define CFG_SPDSTS	0x60000000
#define CFG_SPDSTS1	0x40000000
#define CFG_SPDSTS0	0x20000000
#define CFG_DUPSTS	0x10000000
#define CFG_TBI_EN	0x01000000
#define CFG_MODE_1000	0x00400000
#define CFG_PINT_CTL	0x001c0000
#define CFG_PINT_DUPSTS	0x00100000
#define CFG_PINT_LNKSTS	0x00080000
#define CFG_PINT_SPDSTS	0x00040000
#define CFG_TMRTEST	0x00020000
#define CFG_MRM_DIS	0x00010000
#define CFG_MWI_DIS	0x00008000
#define CFG_T64ADDR	0x00004000
#define CFG_PCI64_DET	0x00002000
#define CFG_DATA64_EN	0x00001000
#define CFG_M64ADDR	0x00000800
#define CFG_PHY_RST	0x00000400
#define CFG_PHY_DIS	0x00000200
#define CFG_EXTSTS_EN	0x00000100
#define CFG_REQALG	0x00000080
#define CFG_SB		0x00000040
#define CFG_POW		0x00000020
#define CFG_EXD		0x00000010
#define CFG_PESEL	0x00000008
#define CFG_BROM_DIS	0x00000004
#define CFG_EXT_125	0x00000002
#define CFG_BEM		0x00000001

#define EXTSTS_UDPPKT	0x00200000
#define EXTSTS_TCPPKT	0x00080000
#define EXTSTS_IPPKT	0x00020000

#define SPDSTS_POLARITY	(CFG_SPDSTS1 | CFG_SPDSTS0 | CFG_DUPSTS)

#define MIBC_MIBS	0x00000008
#define MIBC_ACLR	0x00000004
#define MIBC_FRZ	0x00000002
#define MIBC_WRN	0x00000001

#define RXCFG_AEP	0x80000000
#define RXCFG_ARP	0x40000000
#define RXCFG_STRIPCRC	0x20000000
#define RXCFG_RX_FD	0x10000000
#define RXCFG_ALP	0x08000000
#define RXCFG_AIRL	0x04000000
#define RXCFG_MXDMA	0x00700000
#define RXCFG_MXDMA0	0x00100000
#define RXCFG_MXDMA64	0x00600000
#define RXCFG_DRTH	0x0000003e
#define RXCFG_DRTH0	0x00000002

#define RFCR_RFEN	0x80000000
#define RFCR_AAB	0x40000000
#define RFCR_AAM	0x20000000
#define RFCR_AAU	0x10000000
#define RFCR_APM	0x08000000
#define RFCR_APAT	0x07800000
#define RFCR_APAT3	0x04000000
#define RFCR_APAT2	0x02000000
#define RFCR_APAT1	0x01000000
#define RFCR_APAT0	0x00800000
#define RFCR_AARP	0x00400000
#define RFCR_MHEN	0x00200000
#define RFCR_UHEN	0x00100000
#define RFCR_ULM	0x00080000

#define VRCR_RUDPE	0x00000080
#define VRCR_RTCPE	0x00000040
#define VRCR_RIPE	0x00000020
#define VRCR_IPEN	0x00000010
#define VRCR_DUTF	0x00000008
#define VRCR_DVTF	0x00000004
#define VRCR_VTREN	0x00000002
#define VRCR_VTDEN	0x00000001

#define VTCR_PPCHK	0x00000008
#define VTCR_GCHK	0x00000004
#define VTCR_VPPTI	0x00000002
#define VTCR_VGTI	0x00000001

#define CR		0x00
#define CFG		0x04
#define MEAR		0x08
#define PTSCR		0x0c
#define	ISR		0x10
#define	IMR		0x14
#define	IER		0x18
#define	IHR		0x1c
#define TXDP		0x20
#define TXDP_HI		0x24
#define TXCFG		0x28
#define GPIOR		0x2c
#define RXDP		0x30
#define RXDP_HI		0x34
#define RXCFG		0x38
#define PQCR		0x3c
#define WCSR		0x40
#define PCR		0x44
#define RFCR		0x48
#define RFDR		0x4c

#define SRR		0x58

#define VRCR		0xbc
#define VTCR		0xc0
#define VDR		0xc4
#define CCSR		0xcc

#define __kick_rx(dev)	writel(CR_RXE, dev->base + CR)

#define kick_rx(dev) do { \
	dprintk("kick_rx: maybe kicking\n"); \
	if (test_and_clear_bit(0, &dev->rx_info.idle)) { \
		dprintk("actually kicking\n"); \
		writel(dev->rx_info.phy_descs + (4 * DESC_SIZE * dev->rx_info.next_rx), dev->base + RXDP); \
		if (dev->rx_info.next_rx == dev->rx_info.next_empty) \
			printk(KERN_DEBUG "%s: uh-oh: next_rx == next_empty???\n", dev->net_dev.name);\
		__kick_rx(dev); \
	} \
} while(0)

#ifdef USE_64BIT_ADDR
typedef u64	hw_addr_t;
#else
typedef u32	hw_addr_t;
#endif

#define HW_ADDR_LEN	(sizeof(hw_addr_t))

#define LINK		0
#define BUFPTR		(LINK + HW_ADDR_LEN/4)
#define CMDSTS		(BUFPTR + HW_ADDR_LEN/4)
#define EXTSTS		(CMDSTS + 4/4)
#define DRV_NEXT	(EXTSTS + 4/4)

#define CMDSTS_OWN	0x80000000
#define CMDSTS_MORE	0x40000000
#define CMDSTS_INTR	0x20000000
#define CMDSTS_ERR	0x10000000
#define CMDSTS_OK	0x08000000

#define CMDSTS_DEST_MASK	0x01800000
#define CMDSTS_DEST_SELF	0x00800000
#define CMDSTS_DEST_MULTI	0x01000000

#define DESC_SIZE	8		/* Should be cache line sized */

struct rx_info {
	spinlock_t	lock;
	int		up;
	long		idle;

	struct sk_buff	*skbs[NR_RX_DESC];

	unsigned	next_rx, next_empty;

	u32		*descs;
	dma_addr_t	phy_descs;
};


struct ns83820 {
	struct net_device	net_dev;
	struct net_device_stats	stats;
	u8			*base;

	struct pci_dev		*pci_dev;

	struct rx_info		rx_info;

	unsigned		ihr;
	struct tq_struct	tq_refill;

	/* protects everything below.  irqsave when using. */
	spinlock_t		misc_lock;

	u32			CFG_cache;

	u32			MEAR_cache;
	u32			IMR_cache;
	struct eeprom		ee;


	spinlock_t	tx_lock;

	long		tx_idle;
	u32		tx_done_idx;
	u32		tx_idx;
	volatile u32	tx_free_idx;	/* idx of free desc chain */
	u32		tx_intr_idx;

	struct sk_buff	*tx_skbs[NR_TX_DESC];

	char		pad[16] __attribute__((aligned(16)));
	u32		*tx_descs;
	dma_addr_t	tx_phy_descs;
};

//free = (tx_done_idx + NR_TX_DESC-2 - free_idx) % NR_TX_DESC
#define start_tx_okay(dev)	\
	(((NR_TX_DESC-2 + dev->tx_done_idx - dev->tx_free_idx) % NR_TX_DESC) > NR_TX_DESC/2)


/* Packet Receiver
 *
 * The hardware supports linked lists of receive descriptors for
 * which ownership is transfered back and forth by means of an
 * ownership bit.  While the hardware does support the use of a
 * ring for receive descriptors, we only make use of a chain in
 * an attempt to reduce bus traffic under heavy load scenarios.
 * This will also make bugs a bit more obvious.  The current code
 * only makes use of a single rx chain; I hope to implement
 * priority based rx for version 1.0.  Goal: even under overload
 * conditions, still route realtime traffic with as low jitter as
 * possible.
 */
#ifdef USE_64BIT_ADDR
static inline void build_rx_desc64(struct ns83820 *dev, u32 *desc, u64 link, u64 buf, u32 cmdsts, u32 extsts)
{
	desc[0] = link;
	desc[1] = link >> 32;
	desc[2] = buf;
	desc[3] = buf >> 32;
	desc[5] = extsts;
	mb();
	desc[4] = cmdsts;
}

#define build_rx_desc	build_rx_desc64
#else

static inline void build_rx_desc32(struct ns83820 *dev, u32 *desc, u32 link, u32 buf, u32 cmdsts, u32 extsts)
{
	desc[0] = link;
	desc[1] = buf;
	desc[3] = extsts;
	mb();
	desc[2] = cmdsts;
}

#define build_rx_desc	build_rx_desc32
#endif

#define nr_rx_empty(dev) ((NR_RX_DESC-2 + dev->rx_info.next_rx - dev->rx_info.next_empty) % NR_RX_DESC)
static inline int ns83820_add_rx_skb(struct ns83820 *dev, struct sk_buff *skb)
{
	unsigned next_empty;
	u32 cmdsts;
	u32 *sg;
	hw_addr_t buf;

	next_empty = dev->rx_info.next_empty;

	/* don't overrun last rx marker */
	if (nr_rx_empty(dev) <= 2) {
		kfree_skb(skb);
		return 1;
	}

#if 0
	dprintk("next_empty[%d] nr_used[%d] next_rx[%d]\n",
		dev->rx_info.next_empty,
		dev->rx_info.nr_used,
		dev->rx_info.next_rx
		);
#endif

	sg = dev->rx_info.descs + (next_empty * DESC_SIZE);
	if (dev->rx_info.skbs[next_empty])
		BUG();
	dev->rx_info.skbs[next_empty] = skb;

	dev->rx_info.next_empty = (next_empty + 1) % NR_RX_DESC;
	cmdsts = RX_BUF_SIZE | CMDSTS_INTR;
	buf = pci_map_single(dev->pci_dev, skb->tail, RX_BUF_SIZE, PCI_DMA_FROMDEVICE);
	build_rx_desc(dev, sg, 0, buf, cmdsts, 0);
	/* update link of previous rx */
	if (next_empty != dev->rx_info.next_rx)
		dev->rx_info.descs[((NR_RX_DESC + next_empty - 1) % NR_RX_DESC) * DESC_SIZE] = dev->rx_info.phy_descs + (next_empty * DESC_SIZE * 4);

	return 0;
}

static int rx_refill(struct ns83820 *dev, int gfp)
{
	unsigned i;
	long flags = 0;

	dprintk("rx_refill(%p)\n", dev);
	if (gfp == GFP_ATOMIC)
		spin_lock_irqsave(&dev->rx_info.lock, flags);
	for (i=0; i<NR_RX_DESC; i++) {
		struct sk_buff *skb;
		long res;
		skb = __dev_alloc_skb(RX_BUF_SIZE+16, gfp);
		if (!skb)
			break;

		res = (long)skb->tail & 0xf;
		res = 0x10 - res;
		res &= 0xf;
		skb_reserve(skb, res);

		skb->dev = &dev->net_dev;
		if (gfp != GFP_ATOMIC)
			spin_lock_irqsave(&dev->rx_info.lock, flags);
		res = ns83820_add_rx_skb(dev, skb);
		if (gfp != GFP_ATOMIC)
			spin_unlock_irqrestore(&dev->rx_info.lock, flags);
		if (res) {
			i = 1;
			break;
		}
	}
	if (gfp == GFP_ATOMIC)
		spin_unlock_irqrestore(&dev->rx_info.lock, flags);

	return i ? 0 : -ENOMEM;
}

/* REFILL */
static inline void queue_refill(void *_dev)
{
	struct ns83820 *dev = _dev;

	rx_refill(dev, GFP_KERNEL);
	if (dev->rx_info.up)
		kick_rx(dev);
}

static inline void clear_rx_desc(struct ns83820 *dev, unsigned i)
{
	build_rx_desc(dev, dev->rx_info.descs + (DESC_SIZE * i), 0, 0, CMDSTS_OWN, 0);
}

static void phy_intr(struct ns83820 *dev)
{
	static char *speeds[] = { "10", "100", "1000", "1000(?)" };
	u32 cfg, new_cfg;

	new_cfg = dev->CFG_cache & ~(CFG_SB | CFG_MODE_1000 | CFG_SPDSTS);
	cfg = readl(dev->base + CFG) ^ SPDSTS_POLARITY;

	if (cfg & CFG_SPDSTS1)
		new_cfg |= CFG_MODE_1000 | CFG_SB;
	else
		new_cfg &= ~CFG_MODE_1000 | CFG_SB;

	if ((cfg & CFG_LNKSTS) && ((new_cfg ^ dev->CFG_cache) & CFG_MODE_1000)) {
		writel(new_cfg, dev->base + CFG);
		dev->CFG_cache = new_cfg;
	}

	dev->CFG_cache &= ~CFG_SPDSTS;
	dev->CFG_cache |= cfg & CFG_SPDSTS;

	if (cfg & CFG_LNKSTS) {
		netif_start_queue(&dev->net_dev);
		netif_wake_queue(&dev->net_dev);
	} else {
		netif_stop_queue(&dev->net_dev);
	}

	if (cfg & CFG_LNKSTS)
		printk(KERN_INFO "%s: link now %s mbps, %s duplex and up.\n",
			dev->net_dev.name,
			speeds[((cfg / CFG_SPDSTS0) & 3)],
			(cfg & CFG_DUPSTS) ? "full" : "half");
	else
		printk(KERN_INFO "%s: link now down.\n", dev->net_dev.name);
}

static int ns83820_setup_rx(struct ns83820 *dev)
{
	unsigned i;
	int ret;

	dprintk("ns83820_setup_rx(%p)\n", dev);

	dev->rx_info.idle = 1;
	dev->rx_info.next_rx = 0;
	dev->rx_info.next_empty = 0;

	for (i=0; i<NR_RX_DESC; i++)
		clear_rx_desc(dev, i);

	writel(0, dev->base + RXDP_HI);
	writel(dev->rx_info.phy_descs, dev->base + RXDP);

	ret = rx_refill(dev, GFP_KERNEL);
	if (!ret) {
		dprintk("starting receiver\n");
		/* prevent the interrupt handler from stomping on us */
		spin_lock_irq(&dev->rx_info.lock);

		writel(0x0001, dev->base + CCSR);
		writel(0, dev->base + RFCR);
		writel(0x7fc00000, dev->base + RFCR);
		writel(0xffc00000, dev->base + RFCR);

		dev->rx_info.up = 1;

		phy_intr(dev);

		/* Okay, let it rip */
		spin_lock(&dev->misc_lock);
		dev->IMR_cache |= ISR_PHY;
		dev->IMR_cache |= ISR_RXRCMP;
		//dev->IMR_cache |= ISR_RXERR;
		//dev->IMR_cache |= ISR_RXOK;
		dev->IMR_cache |= ISR_RXORN;
		dev->IMR_cache |= ISR_RXSOVR;
		dev->IMR_cache |= ISR_RXDESC;
		dev->IMR_cache |= ISR_RXIDLE;
		dev->IMR_cache |= ISR_TXDESC;
		//dev->IMR_cache |= ISR_TXIDLE;

		writel(dev->IMR_cache, dev->base + IMR);
		writel(1, dev->base + IER);
		spin_unlock(&dev->misc_lock);

		kick_rx(dev);

		spin_unlock_irq(&dev->rx_info.lock);
	}
	return ret;
}

static void ns83820_cleanup_rx(struct ns83820 *dev)
{
	unsigned i;
	long flags;

	dprintk("ns83820_cleanup_rx(%p)\n", dev);

	/* disable receive interrupts */
	spin_lock_irqsave(&dev->misc_lock, flags);
	dev->IMR_cache &= ~(ISR_RXOK | ISR_RXDESC | ISR_RXERR | ISR_RXEARLY | ISR_RXIDLE);
	writel(dev->IMR_cache, dev->base + IMR);
	spin_unlock_irqrestore(&dev->misc_lock, flags);

	/* synchronize with the interrupt handler and kill it */
	dev->rx_info.up = 0;
	synchronize_irq();

	/* touch the pci bus... */
	readl(dev->base + IMR);

	/* assumes the transmitter is already disabled and reset */
	writel(0, dev->base + RXDP_HI);
	writel(0, dev->base + RXDP);

	for (i=0; i<NR_RX_DESC; i++) {
		struct sk_buff *skb = dev->rx_info.skbs[i];
		dev->rx_info.skbs[i] = NULL;
		clear_rx_desc(dev, i);
		if (skb)
			kfree_skb(skb);
	}
}

/* rx_irq
 *	
 */
static void FASTCALL(rx_irq(struct ns83820 *dev));
static void rx_irq(struct ns83820 *dev)
{
	struct rx_info *info = &dev->rx_info;
	unsigned next_rx;
	u32 cmdsts, *desc;
	long flags;
	int nr = 0;

	dprintk("rx_irq(%p)\n", dev);
	dprintk("rxdp: %08x, descs: %08lx next_rx[%d]: %p next_empty[%d]: %p\n",
		readl(dev->base + RXDP),
		(dev->rx_info.phy_descs),
		dev->rx_info.next_rx,
		(dev->rx_info.descs + (DESC_SIZE * dev->rx_info.next_rx)),
		dev->rx_info.next_empty,
		(dev->rx_info.descs + (DESC_SIZE * dev->rx_info.next_empty))
		);

	spin_lock_irqsave(&info->lock, flags);
	if (!info->up)
		goto out;

	dprintk("walking descs\n");
	next_rx = info->next_rx;
	desc = info->descs + (DESC_SIZE * next_rx);
	while ((CMDSTS_OWN & (cmdsts = desc[CMDSTS])) &&
	       (cmdsts != CMDSTS_OWN)) {
		struct sk_buff *skb;
		u32 extsts = desc[EXTSTS];
		dmaaddr_high_t bufptr = *(hw_addr_t *)(desc + BUFPTR);

		dprintk("cmdsts: %08x\n", cmdsts);
		dprintk("link: %08x\n", desc[LINK]);
		dprintk("extsts: %08x\n", desc[EXTSTS]);

		skb = info->skbs[next_rx];
		info->skbs[next_rx] = NULL;
		info->next_rx = (next_rx + 1) % NR_RX_DESC;

		barrier();
		clear_rx_desc(dev, next_rx);

		pci_unmap_single(dev->pci_dev, bufptr,
				 RX_BUF_SIZE, PCI_DMA_FROMDEVICE);
		if (CMDSTS_OK & cmdsts) {
#ifndef __i386__
			struct sk_buff *tmp;
#endif
			int len = cmdsts & 0xffff;
			if (!skb)
				BUG();
			skb_put(skb, len);
#ifndef __i386__	/* I hate the network stack sometimes */
			tmp = __dev_alloc_skb(RX_BUF_SIZE+16, GFP_ATOMIC);
			if (!tmp)
				goto done;
			tmp->dev = &dev->net_dev;
			skb_reserve(tmp, 2);
			memcpy(skb_put(tmp, len), skb->data, len);
			kfree_skb(skb);
			skb = tmp;
#endif
			if (cmdsts & CMDSTS_DEST_MULTI)
				dev->stats.multicast ++;
			dev->stats.rx_packets ++;
			dev->stats.rx_bytes += len;
			if ((extsts & 0x002a0000) && !(extsts & 0x00540000)) {
				skb->ip_summed = CHECKSUM_UNNECESSARY;
			} else {
				skb->ip_summed = CHECKSUM_NONE;
			}
			skb->protocol = eth_type_trans(skb, &dev->net_dev);
			if (NET_RX_DROP == netif_rx(skb))
				dev->stats.rx_dropped ++;
#ifndef __i386__
		done:;
#endif
		} else {
			kfree_skb(skb);
		}

		nr++;
		next_rx = info->next_rx;
		desc = info->descs + (DESC_SIZE * next_rx);
	}
	info->next_rx = next_rx;

out:
	if (0 && !nr) {
		Dprintk("dazed: cmdsts_f: %08x\n", cmdsts);
	}

	spin_unlock_irqrestore(&info->lock, flags);
}


/* Packet Transmit code
 */
static inline void kick_tx(struct ns83820 *dev)
{
	dprintk("kick_tx(%p): tx_idle=%ld, tx_idx=%d free_idx=%d\n",
		dev, dev->tx_idle, dev->tx_idx, dev->tx_free_idx);
	writel(CR_TXE, dev->base + CR);
}

/* no spinlock needed on the transmit irq path as the interrupt handler is serialized */
static void do_tx_done(struct ns83820 *dev)
{
	u32 cmdsts, tx_done_idx, *desc;

	dprintk("do_tx_done(%p)\n", dev);
	tx_done_idx = dev->tx_done_idx;
	desc = dev->tx_descs + (tx_done_idx * DESC_SIZE);

	dprintk("tx_done_idx=%d free_idx=%d cmdsts=%08x\n",
		tx_done_idx, dev->tx_free_idx, desc[CMDSTS]);
	while ((tx_done_idx != dev->tx_free_idx) &&
	       !(CMDSTS_OWN & (cmdsts = desc[CMDSTS])) ) {
		struct sk_buff *skb;

		if (cmdsts & CMDSTS_ERR)
			dev->stats.tx_errors ++;
		if (cmdsts & CMDSTS_OK)
			dev->stats.tx_packets ++;
		if (cmdsts & CMDSTS_OK)
			dev->stats.tx_bytes += cmdsts & 0xffff;

		dprintk("tx_done_idx=%d free_idx=%d cmdsts=%08x\n",
			tx_done_idx, dev->tx_free_idx, desc[CMDSTS]);
		skb = dev->tx_skbs[tx_done_idx];
		dev->tx_skbs[tx_done_idx] = NULL;
		dprintk("done(%p)\n", skb);
		if (skb) {
			pci_unmap_single(dev->pci_dev,
					*(hw_addr_t *)(desc + BUFPTR),
					skb->len,
					PCI_DMA_TODEVICE);
			dev_kfree_skb_irq(skb);
		}

		tx_done_idx = (tx_done_idx + 1) % NR_TX_DESC;
		dev->tx_done_idx = tx_done_idx;
		desc[CMDSTS] = 0;
		barrier();
		desc = dev->tx_descs + (tx_done_idx * DESC_SIZE);
	}

	/* Allow network stack to resume queueing packets after we've
	 * finished transmitting at least 1/4 of the packets in the queue.
	 */
	if (netif_queue_stopped(&dev->net_dev) && start_tx_okay(dev)) {
		dprintk("start_queue(%p)\n", dev);
		netif_start_queue(&dev->net_dev);
		netif_wake_queue(&dev->net_dev);
	}
}

static void ns83820_cleanup_tx(struct ns83820 *dev)
{
	unsigned i;

	for (i=0; i<NR_TX_DESC; i++) {
		struct sk_buff *skb = dev->tx_skbs[i];
		dev->tx_skbs[i] = NULL;
		if (skb)
			dev_kfree_skb(skb);
	}

	memset(dev->tx_descs, 0, NR_TX_DESC * DESC_SIZE * 4);
	set_bit(0, &dev->tx_idle);
}

/* transmit routine.  This code relies on the network layer serializing
 * its calls in, but will run happily in parallel with the interrupt
 * handler.  This code currently has provisions for fragmenting tx buffers
 * while trying to track down a bug in either the zero copy code or
 * the tx fifo (hence the MAX_FRAG_LEN).
 */
#define MAX_FRAG_LEN	8192	/* disabled for now */
static int ns83820_hard_start_xmit(struct sk_buff *skb, struct net_device *_dev)
{
	struct ns83820 *dev = (struct ns83820 *)_dev;
	u32 free_idx, cmdsts, extsts;
	int nr_free, nr_frags;
	unsigned tx_done_idx;
	dmaaddr_high_t buf;
	unsigned len;
	skb_frag_t *frag;
	int stopped = 0;
	int do_intr = 0;
	volatile u32 *first_desc;

	dprintk("ns83820_hard_start_xmit\n");

	nr_frags =  skb_shinfo(skb)->nr_frags;
again:
	if (__builtin_expect(dev->CFG_cache & CFG_LNKSTS, 0)) {
		netif_stop_queue(&dev->net_dev);
		if (__builtin_expect(dev->CFG_cache & CFG_LNKSTS, 0))
			return 1;
		netif_start_queue(&dev->net_dev);
	}

	free_idx = dev->tx_free_idx;
	tx_done_idx = dev->tx_done_idx;
	nr_free = (tx_done_idx + NR_TX_DESC-2 - free_idx) % NR_TX_DESC;
	nr_free -= 1;
	if ((nr_free <= nr_frags) || (nr_free <= 8192 / MAX_FRAG_LEN)) {
		dprintk("stop_queue - not enough(%p)\n", dev);
		netif_stop_queue(&dev->net_dev);

		/* Check again: we may have raced with a tx done irq */
		if (dev->tx_done_idx != tx_done_idx) {
			dprintk("restart queue(%p)\n", dev);
			netif_start_queue(&dev->net_dev);
			goto again;
		}
		return 1;
	}

	if (free_idx == dev->tx_intr_idx) {
		do_intr = 1;
		dev->tx_intr_idx = (dev->tx_intr_idx + NR_TX_DESC/2) % NR_TX_DESC;
	}

	nr_free -= nr_frags;
	if (nr_free < 1) {
		dprintk("stop_queue - last entry(%p)\n", dev);
		netif_stop_queue(&dev->net_dev);
		stopped = 1;
	}

	frag = skb_shinfo(skb)->frags;
	if (!nr_frags)
		frag = 0;
	extsts = 0;
	if (skb->ip_summed == CHECKSUM_HW) {
		extsts |= EXTSTS_IPPKT;
		if (IPPROTO_TCP == skb->nh.iph->protocol)
			extsts |= EXTSTS_TCPPKT;
		else if (IPPROTO_UDP == skb->nh.iph->protocol)
			extsts |= EXTSTS_UDPPKT;
	}

	len = skb->len;
	if (nr_frags)
		len -= skb->data_len;
	buf = pci_map_single(dev->pci_dev, skb->data, len, PCI_DMA_TODEVICE);

	first_desc = dev->tx_descs + (free_idx * DESC_SIZE);

	for (;;) {
		volatile u32 *desc = dev->tx_descs + (free_idx * DESC_SIZE);
		u32 residue = 0;
#if 0
		if (len > MAX_FRAG_LEN) {
			residue = len;
			/* align the start address of the next fragment */
			len = MAX_FRAG_LEN;
			residue -= len;
		}
#endif

		dprintk("frag[%3u]: %4u @ 0x%x%08Lx\n", free_idx, len,
			(unsigned long long)buf);
		free_idx = (free_idx + 1) % NR_TX_DESC;
		desc[LINK] = dev->tx_phy_descs + (free_idx * DESC_SIZE * 4);
		*(hw_addr_t *)(desc + BUFPTR) = buf;
		desc[EXTSTS] = extsts;

		cmdsts = ((nr_frags|residue) ? CMDSTS_MORE : do_intr ? CMDSTS_INTR : 0);
		cmdsts |= (desc == first_desc) ? 0 : CMDSTS_OWN;
		cmdsts |= len;
		desc[CMDSTS] = cmdsts;

		if (residue) {
			buf += len;
			len = residue;
			continue;
		}

		if (!nr_frags)
			break;

		buf = pci_map_single_high(dev->pci_dev, frag->page, 0,
					  frag->size, PCI_DMA_TODEVICE);
		dprintk("frag: buf=%08Lx  page=%08lx\n",
			(long long)buf, (long)(frag->page - mem_map));
		len = frag->size;
		frag++;
		nr_frags--;
	}
	dprintk("done pkt\n");
	dev->tx_skbs[free_idx] = skb;
	first_desc[CMDSTS] |= CMDSTS_OWN;
	dev->tx_free_idx = free_idx;
	kick_tx(dev);

	/* Check again: we may have raced with a tx done irq */
	if (stopped && (dev->tx_done_idx != tx_done_idx) && start_tx_okay(dev))
		netif_start_queue(&dev->net_dev);

	return 0;
}

static void ns83820_update_stats(struct ns83820 *dev)
{
	u8 *base = dev->base;

	dev->stats.rx_errors		+= readl(base + 0x60) & 0xffff;
	dev->stats.rx_crc_errors	+= readl(base + 0x64) & 0xffff;
	dev->stats.rx_missed_errors	+= readl(base + 0x68) & 0xffff;
	dev->stats.rx_frame_errors	+= readl(base + 0x6c) & 0xffff;
	/*dev->stats.rx_symbol_errors +=*/ readl(base + 0x70);
	dev->stats.rx_length_errors	+= readl(base + 0x74) & 0xffff;
	dev->stats.rx_length_errors	+= readl(base + 0x78) & 0xffff;
	/*dev->stats.rx_badopcode_errors += */ readl(base + 0x7c);
	/*dev->stats.rx_pause_count += */  readl(base + 0x80);
	/*dev->stats.tx_pause_count += */  readl(base + 0x84);
	dev->stats.tx_carrier_errors	+= readl(base + 0x88) & 0xff;
}

static struct net_device_stats *ns83820_get_stats(struct net_device *_dev)
{
	struct ns83820 *dev = (void *)_dev;

	/* somewhat overkill */
	spin_lock_irq(&dev->misc_lock);
	ns83820_update_stats(dev);
	spin_unlock_irq(&dev->misc_lock);

	return &dev->stats;
}

static void ns83820_irq(int foo, void *data, struct pt_regs *regs)
{
	struct ns83820 *dev = data;
	int count = 0;
	u32 isr;
	dprintk("ns83820_irq(%p)\n", dev);

	dev->ihr = 0;

	while (count++ < 32 && (isr = readl(dev->base + ISR))) {
		dprintk("irq: %08x\n", isr);

		if (isr & ~(ISR_PHY | ISR_RXDESC | ISR_RXEARLY | ISR_RXOK | ISR_RXERR | ISR_TXIDLE | ISR_TXOK | ISR_TXDESC))
			Dprintk("odd isr? 0x%08x\n", isr);

	if ((ISR_RXEARLY | ISR_RXIDLE | ISR_RXORN | ISR_RXDESC | ISR_RXOK | ISR_RXERR) & isr) {
 		if (ISR_RXIDLE & isr) {
			dev->rx_info.idle = 1;
			Dprintk("oh dear, we are idle\n");
		}

		if ((ISR_RXDESC) & isr) {
			rx_irq(dev);
			writel(4, dev->base + IHR);
		}

		if (nr_rx_empty(dev) >= NR_RX_DESC/4) {
			if (dev->rx_info.up) {
				rx_refill(dev, GFP_ATOMIC);
				kick_rx(dev);
			}
		}

		if (dev->rx_info.up && nr_rx_empty(dev) > NR_RX_DESC*3/4)
			schedule_task(&dev->tq_refill);
		else
			kick_rx(dev);
		if (dev->rx_info.idle)
			Dprintk("BAD\n");
	}

	if (ISR_RXSOVR & isr)
		Dprintk("overrun\n");
	if (ISR_RXORN & isr)
		Dprintk("overrun\n");

	if ((ISR_RXRCMP & isr) && dev->rx_info.up)
		writel(CR_RXE, dev->base + CR);

	if (ISR_TXIDLE & isr) {
		u32 txdp;
		txdp = readl(dev->base + TXDP);
		dprintk("txdp: %08x\n", txdp);
		txdp -= dev->tx_phy_descs;
		dev->tx_idx = txdp / (DESC_SIZE * 4);
		if (dev->tx_idx >= NR_TX_DESC) {
			printk(KERN_ALERT "%s: BUG -- txdp out of range\n", dev->net_dev.name);
			dev->tx_idx = 0;
		}
		if (dev->tx_idx != dev->tx_free_idx)
			writel(CR_TXE, dev->base + CR);
			//kick_tx(dev);
		else
			dev->tx_idle = 1;
		mb();
		if (dev->tx_idx != dev->tx_free_idx)
			kick_tx(dev);
	}

	/* Defer tx ring processing until more than a minimum amount of
	 * work has accumulated
	 */
	if ((ISR_TXDESC | ISR_TXIDLE) & isr)
		do_tx_done(dev);

	if (ISR_MIB & isr) {
		spin_lock(&dev->misc_lock);
		ns83820_update_stats(dev);
		spin_unlock(&dev->misc_lock);
	}

	if (ISR_PHY & isr)
		phy_intr(dev);
	}

#if 0	/* Still working on the interrupt mitigation strategy */
	if (dev->ihr)
		writel(dev->ihr, dev->base + IHR);
#endif
}

static void ns83820_do_reset(struct ns83820 *dev, u32 which)
{
	Dprintk("resetting chip...\n");
	writel(which, dev->base + CR);
	do {
		schedule();
	} while (readl(dev->base + CR) & which);
	Dprintk("okay!\n");
}

static int ns83820_stop(struct net_device *_dev)
{
	struct ns83820 *dev = (struct ns83820 *)_dev;

	/* FIXME: protect against interrupt handler? */

	/* disable interrupts */
	writel(0, dev->base + IMR);
	writel(0, dev->base + IER);
	readl(dev->base + IER);

	dev->rx_info.up = 0;
	synchronize_irq();

	ns83820_do_reset(dev, CR_RST);

	synchronize_irq();

	dev->IMR_cache &= ~(ISR_TXURN | ISR_TXIDLE | ISR_TXERR | ISR_TXDESC | ISR_TXOK);
	ns83820_cleanup_rx(dev);
	ns83820_cleanup_tx(dev);

	return 0;
}

static int ns83820_open(struct net_device *_dev)
{
	struct ns83820 *dev = (struct ns83820 *)_dev;
	unsigned i;
	u32 desc;
	int ret;

	dprintk("ns83820_open\n");

	writel(0, dev->base + PQCR);

	ret = ns83820_setup_rx(dev);
	if (ret)
		goto failed;

	memset(dev->tx_descs, 0, 4 * NR_TX_DESC * DESC_SIZE);
	for (i=0; i<NR_TX_DESC; i++) {
		*(hw_addr_t *)(dev->tx_descs + (i * DESC_SIZE) + LINK)
				= dev->tx_phy_descs
				  + ((i+1) % NR_TX_DESC) * DESC_SIZE * 4;
	}

	dev->tx_idx = 0;
	dev->tx_done_idx = 0;
	desc = dev->tx_phy_descs;
	writel(0, dev->base + TXDP_HI);
	writel(desc, dev->base + TXDP);

//printk("IMR: %08x / %08x\n", readl(dev->base + IMR), dev->IMR_cache);

	set_bit(0, &dev->tx_idle);
	netif_start_queue(&dev->net_dev);	/* FIXME: wait for phy to come up */

	return 0;

failed:
	ns83820_stop(_dev);
	return ret;
}

#if 0	/* FIXME: implement this! */
static void ns83820_tx_timeout(struct net_device *_dev)
{
	struct ns83820 *dev = (struct ns83820 *)_dev;

	printk("ns83820_tx_timeout\n");
}
#endif

static void ns83820_getmac(struct ns83820 *dev, u8 *mac)
{
	unsigned i;
	for (i=0; i<3; i++) {
		u32 data;
#if 0	/* I've left this in as an example of how to use eeprom.h */
		data = eeprom_readw(&dev->ee, 0xa + 2 - i);
#else
		writel(i*2, dev->base + RFCR);
		data = readl(dev->base + RFDR);
#endif
		*mac++ = data;
		*mac++ = data >> 8;
	}
}

static int ns83820_change_mtu(struct net_device *_dev, int new_mtu)
{
	if (new_mtu > RX_BUF_SIZE)
		return -EINVAL;
	_dev->mtu = new_mtu;
	return 0;
}

static void ns83820_set_multicast(struct net_device *_dev)
{
	struct ns83820 *dev = (void *)_dev;
	u8 *rfcr = dev->base + RFCR;
	u32 and_mask = 0xffffffff;
	u32 or_mask = 0;

	if (dev->net_dev.flags & IFF_PROMISC)
		or_mask |= RFCR_AAU | RFCR_AAM;
	else
		and_mask &= ~(RFCR_AAU | RFCR_AAM);

	if (dev->net_dev.flags & IFF_ALLMULTI)
		or_mask |= RFCR_AAM;
	else
		and_mask &= ~RFCR_AAM;

	spin_lock_irq(&dev->misc_lock);
	writel((readl(rfcr) & and_mask) | or_mask, rfcr);
	spin_unlock_irq(&dev->misc_lock);
}

static int __devinit ns83820_init_one(struct pci_dev *pci_dev, const struct pci_device_id *id)
{
	struct ns83820 *dev;
	long addr;
	int err;

	dev = (struct ns83820 *)alloc_etherdev((sizeof *dev) - (sizeof dev->net_dev));
	err = -ENOMEM;
	if (!dev)
		goto out;

	spin_lock_init(&dev->rx_info.lock);
	spin_lock_init(&dev->tx_lock);
	spin_lock_init(&dev->misc_lock);
	dev->pci_dev = pci_dev;

	dev->ee.cache = &dev->MEAR_cache;
	dev->ee.lock = &dev->misc_lock;
	dev->net_dev.owner = THIS_MODULE;

	PREPARE_TQUEUE(&dev->tq_refill, queue_refill, dev);

	err = pci_enable_device(pci_dev);
	if (err) {
		printk(KERN_INFO "ns83820: pci_enable_dev: %d\n", err);
		goto out_free;
	}

	pci_set_master(pci_dev);
	addr = pci_resource_start(pci_dev, 1);
	dev->base = ioremap_nocache(addr, PAGE_SIZE);
	dev->tx_descs = pci_alloc_consistent(pci_dev,
			4 * DESC_SIZE * NR_TX_DESC, &dev->tx_phy_descs);
	dev->rx_info.descs = pci_alloc_consistent(pci_dev,
			4 * DESC_SIZE * NR_RX_DESC, &dev->rx_info.phy_descs);
	err = -ENOMEM;
	if (!dev->base || !dev->tx_descs || !dev->rx_info.descs)
		goto out_disable;

	dprintk("%p: %08lx  %p: %08lx\n", dev->tx_descs, dev->tx_phy_descs,
		dev->rx_info.descs, dev->rx_info.phy_descs);
	/* disable interrupts */
	writel(0, dev->base + IMR);
	writel(0, dev->base + IER);
	readl(dev->base + IER);

	dev->IMR_cache = 0;

	setup_ee_mem_bitbanger(&dev->ee, (long)dev->base + MEAR, 3, 2, 1, 0,
		0);

	err = request_irq(pci_dev->irq, ns83820_irq, SA_SHIRQ,
			  dev->net_dev.name, dev);
	if (err) {
		printk(KERN_INFO "ns83820: unable to register irq %d\n",
			pci_dev->irq);
		goto out_unmap;
	}

	dev->net_dev.open = ns83820_open;
	dev->net_dev.stop = ns83820_stop;
	dev->net_dev.hard_start_xmit = ns83820_hard_start_xmit;
	dev->net_dev.change_mtu = ns83820_change_mtu;
	dev->net_dev.get_stats = ns83820_get_stats;
	dev->net_dev.change_mtu = ns83820_change_mtu;
	dev->net_dev.set_multicast_list = ns83820_set_multicast;
	//FIXME: dev->net_dev.tx_timeout = ns83820_tx_timeout;

	pci_set_drvdata(pci_dev, dev);

	ns83820_do_reset(dev, CR_RST);

	dprintk("start bist\n");
	writel(PTSCR_EEBIST_EN, dev->base + PTSCR);
	do {
		schedule();
	} while (readl(dev->base + PTSCR) & PTSCR_EEBIST_EN);
	dprintk("done bist\n");

	dprintk("start eeload\n");
	writel(PTSCR_EELOAD_EN, dev->base + PTSCR);
	do {
		schedule();
	} while (readl(dev->base + PTSCR) & PTSCR_EELOAD_EN);
	dprintk("done eeload\n");

	/* I love config registers */
	dev->CFG_cache = readl(dev->base + CFG);

	if ((dev->CFG_cache & CFG_PCI64_DET)) {
		printk("%s: enabling 64 bit PCI.\n", dev->net_dev.name);
		dev->CFG_cache |= CFG_T64ADDR | CFG_DATA64_EN;
	} else {
		printk("%s: disabling 64 bit PCI.\n", dev->net_dev.name);
		dev->CFG_cache &= ~(CFG_T64ADDR | CFG_DATA64_EN);
	}
	dev->CFG_cache &= (CFG_TBI_EN  | CFG_MRM_DIS   | CFG_MWI_DIS |
			   CFG_T64ADDR | CFG_DATA64_EN | CFG_EXT_125 |
			   CFG_M64ADDR);
	dev->CFG_cache |= CFG_PINT_DUPSTS | CFG_PINT_LNKSTS | CFG_PINT_SPDSTS |
			  CFG_EXTSTS_EN   | CFG_EXD         | CFG_PESEL;
	dev->CFG_cache |= CFG_REQALG;
	dev->CFG_cache |= CFG_POW;
#ifdef USE_64BIT_ADDR
	dev->CFG_cache |= CFG_M64ADDR;
	printk("using 64 bit addressing\n");
#endif
#ifdef __LITTLE_ENDIAN
	dev->CFG_cache &= ~CFG_BEM;
#elif defined(__BIG_ENDIAN)
	dev->CFG_cache |= CFG_BEM;
#else
#error This driver only works for big or little endian!!!
#endif

	writel(dev->CFG_cache, dev->base + CFG);
	dprintk("CFG: %08x\n", dev->CFG_cache);

	if (readl(dev->base + SRR))
		writel(readl(dev->base+0x20c) | 0xfe00, dev->base + 0x20c);

	/* Note!  The DMA burst size interacts with packet
	 * transmission, such that the largest packet that
	 * can be transmitted is 8192 - FLTH - burst size.
	 * If only the transmit fifo was larger...
	 */
	writel(TXCFG_CSI | TXCFG_HBI | TXCFG_ATP | TXCFG_MXDMA1024
		| ((1600 / 32) * 0x100),
		dev->base + TXCFG);

	/* Flush the interrupt holdoff timer */
	writel(0x000, dev->base + IHR);
	writel(0x100, dev->base + IHR);

	/* Set Rx to full duplex, don't accept runt, errored, long or length
	 * range errored packets.  Set MXDMA to 7 => 512 word burst
	 */
	writel(RXCFG_AEP | RXCFG_ARP | RXCFG_AIRL | RXCFG_RX_FD
		| RXCFG_ALP
		| RXCFG_MXDMA | 0, dev->base + RXCFG);

	/* Disable priority queueing */
	writel(0, dev->base + PQCR);

	/* Enable IP checksum validation and detetion of VLAN headers.
	 * Note: do not set the reject options as at least the 0x102
	 * revision of the chip does not properly accept IP fragments
	 * at least for UDP.
	 */
	writel(VRCR_IPEN | VRCR_VTDEN, dev->base + VRCR);

	/* Enable per-packet TCP/UDP/IP checksumming */
	writel(VTCR_PPCHK, dev->base + VTCR);

	/* Disable Pause frames */
	writel(0, dev->base + PCR);

	/* Disable Wake On Lan */
	writel(0, dev->base + WCSR);

	ns83820_getmac(dev, dev->net_dev.dev_addr);

	/* Yes, we support dumb IP checksum on transmit */
	dev->net_dev.features |= NETIF_F_SG;
	dev->net_dev.features |= NETIF_F_IP_CSUM;
#if defined(USE_64BIT_ADDR) || defined(CONFIG_HIGHMEM4G)
	dev->net_dev.features |= NETIF_F_HIGHDMA;
#endif

	register_netdev(&dev->net_dev);

	printk(KERN_INFO "%s: ns83820.c v" VERSION ": DP83820 %02x:%02x:%02x:%02x:%02x:%02x pciaddr=0x%08lx irq=%d rev 0x%x\n",
		dev->net_dev.name,
		dev->net_dev.dev_addr[0], dev->net_dev.dev_addr[1],
		dev->net_dev.dev_addr[2], dev->net_dev.dev_addr[3],
		dev->net_dev.dev_addr[4], dev->net_dev.dev_addr[5],
		addr, pci_dev->irq,
		(unsigned)readl(dev->base + SRR)
		);

	return 0;

out_unmap:
	iounmap(dev->base);
out_disable:
	pci_free_consistent(pci_dev, 4 * DESC_SIZE * NR_TX_DESC, dev->tx_descs, dev->tx_phy_descs);
	pci_free_consistent(pci_dev, 4 * DESC_SIZE * NR_RX_DESC, dev->rx_info.descs, dev->rx_info.phy_descs);
	pci_disable_device(pci_dev);
out_free:
	kfree(dev);
	pci_set_drvdata(pci_dev, NULL);
out:
	return err;
}

static void __devexit ns83820_remove_one(struct pci_dev *pci_dev)
{
	struct ns83820	*dev = pci_get_drvdata(pci_dev);

	if (!dev)			/* paranoia */
		return;

	writel(0, dev->base + IMR);	/* paranoia */
	writel(0, dev->base + IER);
	readl(dev->base + IER);

	unregister_netdev(&dev->net_dev);
	free_irq(dev->pci_dev->irq, dev);
	iounmap(dev->base);
	pci_free_consistent(dev->pci_dev, 4 * DESC_SIZE * NR_TX_DESC,
			dev->tx_descs, dev->tx_phy_descs);
	pci_free_consistent(dev->pci_dev, 4 * DESC_SIZE * NR_RX_DESC,
			dev->rx_info.descs, dev->rx_info.phy_descs);
	pci_disable_device(dev->pci_dev);
	kfree(dev);
	pci_set_drvdata(pci_dev, NULL);
}

static struct pci_device_id ns83820_pci_tbl[] __devinitdata = {
	{ 0x100b, 0x0022, PCI_ANY_ID, PCI_ANY_ID, 0, 0, },
	{ 0, },
};

static struct pci_driver driver = {
	name:		"ns83820",
	id_table:	ns83820_pci_tbl,
	probe:		ns83820_init_one,
	remove:		ns83820_remove_one,
#if 0	/* FIXME: implement */
	suspend:	,
	resume:		,
#endif
};


static int __init ns83820_init(void)
{
	printk(KERN_INFO "ns83820.c: National Semiconductor DP83820 10/100/100 driver.\n");
	return pci_module_init(&driver);
}

static void __exit ns83820_exit(void)
{
	pci_unregister_driver(&driver);
}

MODULE_AUTHOR("Benjamin LaHaise <bcrl@redhat.com>");
MODULE_DESCRIPTION("National Semiconductor DP83820 10/100/1000 driver");
MODULE_LICENSE("GPL");

MODULE_DEVICE_TABLE(pci, ns83820_pci_tbl);

module_init(ns83820_init);
module_exit(ns83820_exit);
