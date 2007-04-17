/*
 *  PS3 Platfom gelic network driver.
 *
 * Copyright (C) 2006 Sony Computer Entertainment Inc.
 *
 *  this file is based on: spider_net.c
 *
 * Network device driver for Cell Processor-Based Blade
 *
 * (C) Copyright IBM Corp. 2005
 *
 * Authors : Utz Bacher <utz.bacher@de.ibm.com>
 *           Jens Osterkamp <Jens.Osterkamp@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define DEBUG 1

#include <linux/compiler.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/firmware.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/tcp.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <asm/bitops.h>
#include <asm/pci-bridge.h>
#include <net/checksum.h>
#include <asm/io.h>
#include <asm/ps3.h>
#include <asm/lv1call.h>

#define GELIC_NET_DRV_NAME "Gelic Network Driver"
#define GELIC_NET_DRV_VERSION "1.0"

#define GELIC_NET_DEBUG

#ifdef GELIC_NET_DEBUG
#define DPRINTK(fmt,arg...)   printk(KERN_ERR fmt ,##arg)
#define DPRINTKK(fmt,arg...)  printk(KERN_ERR fmt ,##arg)
#else
#define DPRINTK(fmt,arg...)
#define DPRINTKK(fmt,arg...)
#endif

#define GELIC_NET_ETHTOOL               /* use ethtool */

/* ioctl */
#define GELIC_NET_GET_MODE              (SIOCDEVPRIVATE + 0)
#define GELIC_NET_SET_MODE              (SIOCDEVPRIVATE + 1)

/* descriptors */
#define GELIC_NET_RX_DESCRIPTORS        128 /* num of descriptors */
#define GELIC_NET_TX_DESCRIPTORS        128 /* num of descriptors */

#define GELIC_NET_MAX_MTU               2308
#define GELIC_NET_MIN_MTU               64
#define GELIC_NET_RXBUF_ALIGN           128
#define GELIC_NET_RX_CSUM_DEFAULT       1 /* hw chksum */
#define GELIC_NET_WATCHDOG_TIMEOUT      5*HZ
#define GELIC_NET_NAPI_WEIGHT           64
#define GELIC_NET_BROADCAST_ADDR        0xffffffffffff
#define GELIC_NET_VLAN_POS              (VLAN_ETH_ALEN * 2)
#define GELIC_NET_VLAN_MAX              4
#define GELIC_NET_MC_COUNT_MAX          32 /* multicast address list */

enum gelic_net_int0_status {
	GELIC_NET_GDTDCEINT  = 24,
	GELIC_NET_GRFANMINT  = 28,
};

/* GHIINT1STS bits */
enum gelic_net_int1_status {
	GELIC_NET_GDADCEINT = 14,
};

/* interrupt mask */
#define GELIC_NET_TXINT                   (1L << (GELIC_NET_GDTDCEINT + 32))

#define GELIC_NET_RXINT0                  (1L << (GELIC_NET_GRFANMINT + 32))
#define GELIC_NET_RXINT1                  (1L << GELIC_NET_GDADCEINT)
#define GELIC_NET_RXINT                   (GELIC_NET_RXINT0 | GELIC_NET_RXINT1)

 /* descriptor data_status bits */
#define GELIC_NET_RXIPCHK                 29
#define GELIC_NET_TCPUDPIPCHK             28
#define GELIC_NET_DATA_STATUS_CHK_MASK    (1 << GELIC_NET_RXIPCHK | \
                                           1 << GELIC_NET_TCPUDPIPCHK)

/* descriptor data_error bits */
#define GELIC_NET_RXIPCHKERR              27
#define GELIC_NET_RXTCPCHKERR             26
#define GELIC_NET_DATA_ERROR_CHK_MASK     (1 << GELIC_NET_RXIPCHKERR | \
                                           1 << GELIC_NET_RXTCPCHKERR)

#define GELIC_NET_DMAC_CMDSTAT_NOCS       0xa0080000 /* middle of frame */
#define GELIC_NET_DMAC_CMDSTAT_TCPCS      0xa00a0000
#define GELIC_NET_DMAC_CMDSTAT_UDPCS      0xa00b0000
#define GELIC_NET_DMAC_CMDSTAT_END_FRAME  0x00040000 /* end of frame */

#define GELIC_NET_DMAC_CMDSTAT_CHAIN_END  0x00000002 /* RXDCEIS:DMA stopped */

#define GELIC_NET_DESCR_IND_PROC_SHIFT    28
#define GELIC_NET_DESCR_IND_PROC_MASKO    0x0fffffff

/* ignore ipsec ans multicast */
#define GELIC_NET_DATA_ERROR_MASK         0xfdefbfff
/* ignore unmatched sp on sp, drop_packet, multicast address frame*/
#define GELIC_NET_DATA_ERROR_FLG          0x7def8000

enum gelic_net_descr_status {
	GELIC_NET_DESCR_COMPLETE            = 0x00, /* used in rx and tx */
	GELIC_NET_DESCR_RESPONSE_ERROR      = 0x01, /* used in rx and tx */
	GELIC_NET_DESCR_PROTECTION_ERROR    = 0x02, /* used in rx and tx */
	GELIC_NET_DESCR_FRAME_END           = 0x04, /* used in rx */
	GELIC_NET_DESCR_FORCE_END           = 0x05, /* used in rx and tx */
	GELIC_NET_DESCR_CARDOWNED           = 0x0a, /* used in rx and tx */
	GELIC_NET_DESCR_NOT_IN_USE                  /* any other value */
};
#define GELIC_NET_DMAC_CMDSTAT_NOT_IN_USE 0xb0000000

#define GELIC_NET_DESCR_SIZE              32
struct gelic_net_descr {
	/* as defined by the hardware */
	uint32_t buf_addr;
	uint32_t buf_size;
	uint32_t next_descr_addr;
	uint32_t dmac_cmd_status;
	uint32_t result_size;
	uint32_t valid_size;	/* all zeroes for tx */
	uint32_t data_status;
	uint32_t data_error;	/* all zeroes for tx */

	/* used in the driver */
	struct sk_buff *skb;
	dma_addr_t bus_addr;
	struct gelic_net_descr *next;
	struct gelic_net_descr *prev;
	struct vlan_ethhdr vlan;
} __attribute__((aligned(32)));

struct gelic_net_descr_chain {
	/* we walk from tail to head */
	struct gelic_net_descr *head;
	struct gelic_net_descr *tail;
	spinlock_t lock;
};

struct gelic_net_card {
	struct net_device *netdev;
	uint64_t ghiintmask;
	struct ps3_system_bus_device *dev;
	uint32_t vlan_id[GELIC_NET_VLAN_MAX];
	int vlan_index;

	struct gelic_net_descr_chain tx_chain;
	struct gelic_net_descr_chain rx_chain;
	spinlock_t chain_lock;

	struct net_device_stats netdev_stats;
	int rx_csum;
	spinlock_t intmask_lock;

	struct work_struct tx_timeout_task;
	atomic_t tx_timeout_task_counter;
	wait_queue_head_t waitq;

	struct gelic_net_descr *tx_top, *rx_top;

	struct gelic_net_descr descr[0];
};

static int ps3_gelic_param = 1; /* vlan desc support */
#ifdef CONFIG_GELIC_NET_MODULE
module_param(ps3_gelic_param, int, S_IRUGO);
#endif

struct gelic_net_card *gcard;
static uint64_t gelic_irq_status;

static int dmac_status = 0;

/* for lv1_net_control */
#define GELIC_NET_GET_MAC_ADDRESS               0x0000000000000001
#define GELIC_NET_GET_ETH_PORT_STATUS           0x0000000000000002
#define GELIC_NET_SET_NEGOTIATION_MODE          0x0000000000000003
#define GELIC_NET_GET_VLAN_ID                   0x0000000000000004

#define GELIC_NET_LINK_UP                       0x0000000000000001
#define GELIC_NET_FULL_DUPLEX                   0x0000000000000002
#define GELIC_NET_AUTO_NEG                      0x0000000000000004
#define GELIC_NET_SPEED_10                      0x0000000000000010
#define GELIC_NET_SPEED_100                     0x0000000000000020
#define GELIC_NET_SPEED_1000                    0x0000000000000040

#define GELIC_NET_VLAN_ALL                      0x0000000000000001
#define GELIC_NET_VLAN_WIRED                    0x0000000000000002
#define GELIC_NET_VLAN_WIRELESS                 0x0000000000000003
#define GELIC_NET_VLAN_PSP                      0x0000000000000004
#define GELIC_NET_VLAN_PORT0                    0x0000000000000010
#define GELIC_NET_VLAN_PORT1                    0x0000000000000011
#define GELIC_NET_VLAN_PORT2                    0x0000000000000012
#define GELIC_NET_VLAN_DAEMON_CLIENT_BSS        0x0000000000000013
#define GELIC_NET_VLAN_LIBERO_CLIENT_BSS        0x0000000000000014
#define GELIC_NET_VLAN_NO_ENTRY                 -6

#define GELIC_NET_PORT                          2 /* for port status */


MODULE_AUTHOR("SCE Inc.");
MODULE_DESCRIPTION("Gelic Network driver");
MODULE_LICENSE("GPL");

static int rx_descriptors = GELIC_NET_RX_DESCRIPTORS;
static int tx_descriptors = GELIC_NET_TX_DESCRIPTORS;


/* set irq_mask */
static int
gelic_net_set_irq_mask(struct gelic_net_card *card, uint64_t mask)
{
	uint64_t status = 0;

	status = lv1_net_set_interrupt_mask(card->dev->did.bus_id,
		card->dev->did.dev_id, mask, 0);
	if (status) {
		printk("lv1_net_set_interrupt_mask failed, status=%ld\n",
			status);
	}
	return status;
}

/**
 * gelic_net_get_descr_status -- returns the status of a descriptor
 * @descr: descriptor to look at
 *
 * returns the status as in the dmac_cmd_status field of the descriptor
 */
enum gelic_net_descr_status
gelic_net_get_descr_status(struct gelic_net_descr *descr)
{
	uint32_t cmd_status;

	rmb();
	cmd_status = descr->dmac_cmd_status;
	rmb();
	cmd_status >>= GELIC_NET_DESCR_IND_PROC_SHIFT;
	return cmd_status;
}

/**
 * gelic_net_set_descr_status -- sets the status of a descriptor
 * @descr: descriptor to change
 * @status: status to set in the descriptor
 *
 * changes the status to the specified value. Doesn't change other bits
 * in the status
 */
static void
gelic_net_set_descr_status(struct gelic_net_descr *descr,
			    enum gelic_net_descr_status status)
{
	uint32_t cmd_status;

	/* read the status */
	mb();
	cmd_status = descr->dmac_cmd_status;
	/* clean the upper 4 bits */
	cmd_status &= GELIC_NET_DESCR_IND_PROC_MASKO;
	/* add the status to it */
	cmd_status |= ((uint32_t)status)<<GELIC_NET_DESCR_IND_PROC_SHIFT;
	/* and write it back */
	descr->dmac_cmd_status = cmd_status;
	wmb();
}

/**
 * gelic_net_free_chain - free descriptor chain
 * @card: card structure
 * @descr_in: address of desc
 */
static void
gelic_net_free_chain(struct gelic_net_card *card,
		      struct gelic_net_descr *descr_in)
{
	struct gelic_net_descr *descr;

	for (descr = descr_in; descr && !descr->bus_addr; descr = descr->next) {
		dma_unmap_single(&card->dev->core, descr->bus_addr,
				 GELIC_NET_DESCR_SIZE, PCI_DMA_BIDIRECTIONAL);
		descr->bus_addr = 0;
	}
}

/**
 * gelic_net_init_chain - links descriptor chain
 * @card: card structure
 * @chain: address of chain
 * @start_descr: address of descriptor array
 * @no: number of descriptors
 *
 * we manage a circular list that mirrors the hardware structure,
 * except that the hardware uses bus addresses.
 *
 * returns 0 on success, <0 on failure
 */
static int
gelic_net_init_chain(struct gelic_net_card *card,
		       struct gelic_net_descr_chain *chain,
		       struct gelic_net_descr *start_descr, int no)
{
	int i;
	struct gelic_net_descr *descr;

	spin_lock_init(&chain->lock);
	descr = start_descr;
	memset(descr, 0, sizeof(*descr) * no);

	/* set up the hardware pointers in each descriptor */
	for (i=0; i<no; i++, descr++) {
		gelic_net_set_descr_status(descr, GELIC_NET_DESCR_NOT_IN_USE);
		descr->bus_addr =
			dma_map_single(&card->dev->core, descr,
				       GELIC_NET_DESCR_SIZE,
				       PCI_DMA_BIDIRECTIONAL);

		if (descr->bus_addr == DMA_ERROR_CODE)
			goto iommu_error;

		descr->next = descr + 1;
		descr->prev = descr - 1;
	}
	/* do actual chain list */
	(descr-1)->next = start_descr;
	start_descr->prev = (descr-1);

	descr = start_descr;
	for (i=0; i < no; i++, descr++) {
		if (descr->next) {
			descr->next_descr_addr = descr->next->bus_addr;
		} else {
			descr->next_descr_addr = 0;
		}
	}

	chain->head = start_descr;
	chain->tail = start_descr;
	(descr-1)->next_descr_addr = 0; /* last descr */
	return 0;

iommu_error:
	descr = start_descr;
	for (i=0; i < no; i++, descr++)
		if (descr->bus_addr)
			dma_unmap_single(&card->dev->core, descr->bus_addr,
					 GELIC_NET_DESCR_SIZE,
					 PCI_DMA_BIDIRECTIONAL);
	return -ENOMEM;
}

/**
 * gelic_net_prepare_rx_descr - reinitializes a rx descriptor
 * @card: card structure
 * @descr: descriptor to re-init
 *
 * return 0 on succes, <0 on failure
 *
 * allocates a new rx skb, iommu-maps it and attaches it to the descriptor.
 * Activate the descriptor state-wise
 */
static int
gelic_net_prepare_rx_descr(struct gelic_net_card *card,
			    struct gelic_net_descr *descr)
{
	dma_addr_t buf;
	int error = 0;
	int offset;
	int bufsize;

	if( gelic_net_get_descr_status(descr) !=  GELIC_NET_DESCR_NOT_IN_USE) {
		printk("%s: ERROR status \n", __FUNCTION__);
	}
	/* we need to round up the buffer size to a multiple of 128 */
	bufsize = (GELIC_NET_MAX_MTU + GELIC_NET_RXBUF_ALIGN - 1) &
		(~(GELIC_NET_RXBUF_ALIGN - 1));

	/* and we need to have it 128 byte aligned, therefore we allocate a
	 * bit more */
	/* allocate an skb */
	descr->skb = dev_alloc_skb(bufsize + GELIC_NET_RXBUF_ALIGN - 1);
	if (!descr->skb) {
		if (net_ratelimit())
			printk("Not enough memory to allocate rx buffer\n");
		return -ENOMEM;
	}
	descr->buf_size = bufsize;
	descr->dmac_cmd_status = 0;
	descr->result_size = 0;
	descr->valid_size = 0;
	descr->data_error = 0;

	offset = ((unsigned long)descr->skb->data) &
		(GELIC_NET_RXBUF_ALIGN - 1);
	if (offset)
		skb_reserve(descr->skb, GELIC_NET_RXBUF_ALIGN - offset);
	/* io-mmu-map the skb */
	buf = dma_map_single(&card->dev->core, descr->skb->data,
					GELIC_NET_MAX_MTU,
					PCI_DMA_BIDIRECTIONAL);
	descr->buf_addr = buf;
	if (buf == DMA_ERROR_CODE) {
		dev_kfree_skb_any(descr->skb);
		printk("Could not iommu-map rx buffer\n");
		gelic_net_set_descr_status(descr, GELIC_NET_DESCR_NOT_IN_USE);
	} else {
		gelic_net_set_descr_status(descr, GELIC_NET_DESCR_CARDOWNED);
	}

	return error;
}

/**
 * gelic_net_release_rx_chain - free all rx descr
 * @card: card structure
 *
 */
static void
gelic_net_release_rx_chain(struct gelic_net_card *card)
{
	struct gelic_net_descr_chain *chain = &card->rx_chain;

	while(chain->tail != chain->head) {
		if (chain->tail->skb) {
			dma_unmap_single(&card->dev->core,
						chain->tail->buf_addr,
						chain->tail->skb->len,
						PCI_DMA_BIDIRECTIONAL);
			chain->tail->buf_addr = 0;
			dev_kfree_skb_any(chain->tail->skb);
			chain->tail->skb = NULL;
			chain->tail->dmac_cmd_status =
						GELIC_NET_DESCR_NOT_IN_USE;
			chain->tail = chain->tail->next;
		}
	}
}

/**
 * gelic_net_enable_rxdmac - enables a receive DMA controller
 * @card: card structure
 *
 * gelic_net_enable_rxdmac enables the DMA controller by setting RX_DMA_EN
 * in the GDADMACCNTR register
 */
static void
gelic_net_enable_rxdmac(struct gelic_net_card *card)
{
	uint64_t status;

	status = lv1_net_start_rx_dma(card->dev->did.bus_id,
				card->dev->did.dev_id,
				(uint64_t)card->rx_chain.tail->bus_addr, 0);
	if (status) {
		printk("lv1_net_start_rx_dma failed, status=%ld\n", status);
	}
}

/**
 * gelic_net_refill_rx_chain - refills descriptors/skbs in the rx chains
 * @card: card structure
 *
 * refills descriptors in all chains (last used chain first): allocates skbs
 * and iommu-maps them.
 */
static void
gelic_net_refill_rx_chain(struct gelic_net_card *card)
{
	struct gelic_net_descr_chain *chain;
	int count = 0;

	chain = &card->rx_chain;
	while (chain->head && gelic_net_get_descr_status(chain->head) ==
		GELIC_NET_DESCR_NOT_IN_USE) {
		if (gelic_net_prepare_rx_descr(card, chain->head))
			break;
		count++;
		chain->head = chain->head->next;
	}
}

/**
 * gelic_net_alloc_rx_skbs - allocates rx skbs in rx descriptor chains
 * @card: card structure
 *
 * returns 0 on success, <0 on failure
 */
static int
gelic_net_alloc_rx_skbs(struct gelic_net_card *card)
{
	struct gelic_net_descr_chain *chain;

	chain = &card->rx_chain;
	gelic_net_refill_rx_chain(card);
	chain->head = card->rx_top->prev; /* point to the last */
	return 0;
}

/**
 * gelic_net_release_tx_descr - processes a used tx descriptor
 * @card: card structure
 * @descr: descriptor to release
 *
 * releases a used tx descriptor (unmapping, freeing of skb)
 */
static void
gelic_net_release_tx_descr(struct gelic_net_card *card,
			    struct gelic_net_descr *descr)
{
	struct sk_buff *skb;

  if (!ps3_gelic_param) {
	/* unmap the skb */
	skb = descr->skb;
	dma_unmap_single(&card->dev->core, descr->buf_addr, skb->len,
			 PCI_DMA_BIDIRECTIONAL);

	dev_kfree_skb_any(skb);
  } else {
	if ((descr->data_status & 0x00000001) == 1) { /* end of frame */
		skb = descr->skb;
		dma_unmap_single(&card->dev->core, descr->buf_addr, skb->len,
			 PCI_DMA_BIDIRECTIONAL);
		dev_kfree_skb_any(skb);
	} else {
		dma_unmap_single(&card->dev->core, descr->buf_addr,
			descr->buf_size, PCI_DMA_BIDIRECTIONAL);
	}
  }
	descr->buf_addr = 0;
	descr->buf_size = 0;
	descr->next_descr_addr = 0;
	descr->result_size = 0;
	descr->valid_size = 0;
	descr->data_status = 0;
	descr->data_error = 0;
	descr->skb = NULL;
	card->tx_chain.tail = card->tx_chain.tail->next;

	/* set descr status */
	descr->dmac_cmd_status = GELIC_NET_DMAC_CMDSTAT_NOT_IN_USE;
}

/**
 * gelic_net_release_tx_chain - processes sent tx descriptors
 * @card: adapter structure
 * @stop: net_stop sequence
 *
 * releases the tx descriptors that gelic has finished with
 */
static void
gelic_net_release_tx_chain(struct gelic_net_card *card, int stop)
{
	struct gelic_net_descr_chain *tx_chain = &card->tx_chain;
	enum gelic_net_descr_status status;
	int release = 0;

	for (;tx_chain->head != tx_chain->tail && tx_chain->tail;) {
		status = gelic_net_get_descr_status(tx_chain->tail);
		switch (status) {
		case GELIC_NET_DESCR_RESPONSE_ERROR:
		case GELIC_NET_DESCR_PROTECTION_ERROR:
		case GELIC_NET_DESCR_FORCE_END:
			printk("%s: forcing end of tx descriptor "
			       "with status x%02x\n",
			       card->netdev->name, status);
			card->netdev_stats.tx_dropped++;
			break;

		case GELIC_NET_DESCR_COMPLETE:
			card->netdev_stats.tx_packets++;
			card->netdev_stats.tx_bytes +=
				tx_chain->tail->skb->len;
			break;

		case GELIC_NET_DESCR_CARDOWNED:
		default: /* any other value (== GELIC_NET_DESCR_NOT_IN_USE) */
			goto out;
		}
		gelic_net_release_tx_descr(card, tx_chain->tail);
		release = 1;
	}
out:
	/* status NOT_IN_USE or chain end */
	if (!tx_chain->tail) {
		/* release all chains */
		if(card->tx_chain.head) printk("ERROR tx_chain.head is NULL\n");
		card->tx_chain.tail = card->tx_top;
		card->tx_chain.head = card->tx_top;
	}
	if (!stop && release && netif_queue_stopped(card->netdev)) {
		netif_wake_queue(card->netdev);
	}
}

/**
 * gelic_net_set_multi - sets multicast addresses and promisc flags
 * @netdev: interface device structure
 *
 * gelic_net_set_multi configures multicast addresses as needed for the
 * netdev interface. It also sets up multicast, allmulti and promisc
 * flags appropriately
 */
static void
gelic_net_set_multi(struct net_device *netdev)
{
	int i;
	uint8_t *p;
	uint64_t addr, status;
	struct dev_mc_list *mc;
	struct gelic_net_card *card = netdev_priv(netdev);

	/* clear all multicast address */
	status = lv1_net_remove_multicast_address(card->dev->did.bus_id,
				card->dev->did.dev_id, 0, 1);
	if (status) {
		printk("lv1_net_remove_multicast_address failed, status=%ld\n",\
			status);
	}
	/* set broadcast address */
	status = lv1_net_add_multicast_address(card->dev->did.bus_id,
			card->dev->did.dev_id, GELIC_NET_BROADCAST_ADDR, 0);
	if (status) {
		printk("lv1_net_add_multicast_address failed, status=%ld\n",\
			status);
	}

	if (netdev->flags & IFF_ALLMULTI
		|| netdev->mc_count > GELIC_NET_MC_COUNT_MAX) { /* list max */
		status = lv1_net_add_multicast_address(card->dev->did.bus_id,
				card->dev->did.dev_id,
				0, 1);
		if (status) {
			printk("lv1_net_add_multicast_address failed, status=%ld\n",\
				status);
		}
		return ;
	}

	/* set multicalst address */
	for ( mc = netdev->mc_list; mc; mc = mc->next) {
		addr = 0;
		p = mc->dmi_addr;
		for (i = 0; i < ETH_ALEN; i++) {
			addr <<= 8;
			addr |= *p++;
		}
		status = lv1_net_add_multicast_address(card->dev->did.bus_id,
				card->dev->did.dev_id,
				addr, 0);
		if (status) {
			printk("lv1_net_add_multicast_address failed, status=%ld\n",\
				status);
		}
	}
}

/**
 * gelic_net_disable_rxdmac - disables the receive DMA controller
 * @card: card structure
 *
 * gelic_net_disable_rxdmac terminates processing on the DMA controller by
 * turing off DMA and issueing a force end
 */
static void
gelic_net_disable_rxdmac(struct gelic_net_card *card)
{
	uint64_t status;

	status = lv1_net_stop_rx_dma(card->dev->did.bus_id,
		card->dev->did.dev_id, 0);
	if (status) {
		printk("lv1_net_stop_rx_dma faild, status=%ld\n", status);
	}
}

/**
 * gelic_net_disable_txdmac - disables the transmit DMA controller
 * @card: card structure
 *
 * gelic_net_disable_txdmac terminates processing on the DMA controller by
 * turing off DMA and issueing a force end
 */
static void
gelic_net_disable_txdmac(struct gelic_net_card *card)
{
	uint64_t status;

	status = lv1_net_stop_tx_dma(card->dev->did.bus_id,
		card->dev->did.dev_id, 0);
	if (status) {
		printk("lv1_net_stop_tx_dma faild, status=%ld\n", status);
	}
}

/**
 * gelic_net_stop - called upon ifconfig down
 * @netdev: interface device structure
 *
 * always returns 0
 */
int
gelic_net_stop(struct net_device *netdev)
{
	struct gelic_net_card *card = netdev_priv(netdev);

	netif_poll_disable(netdev);
	netif_stop_queue(netdev);

	/* turn off DMA, force end */
	gelic_net_disable_rxdmac(card);
	gelic_net_disable_txdmac(card);

	gelic_net_set_irq_mask(card, 0);

	/* disconnect event port */
	free_irq(card->netdev->irq, card->netdev);
	ps3_sb_event_receive_port_destroy(&card->dev->did,
		card->dev->interrupt_id, card->netdev->irq);
	card->netdev->irq = NO_IRQ;

	netif_carrier_off(netdev);

	/* release chains */
	gelic_net_release_tx_chain(card, 1);
	gelic_net_release_rx_chain(card);

	gelic_net_free_chain(card, card->tx_top);
	gelic_net_free_chain(card, card->rx_top);

	return 0;
}

/**
 * gelic_net_get_next_tx_descr - returns the next available tx descriptor
 * @card: device structure to get descriptor from
 *
 * returns the address of the next descriptor, or NULL if not available.
 */
static struct gelic_net_descr *
gelic_net_get_next_tx_descr(struct gelic_net_card *card)
{
	if (card->tx_chain.head == NULL) return NULL;
	/* check, if head points to not-in-use descr */
  if (!ps3_gelic_param) {
	if ( card->tx_chain.tail != card->tx_chain.head->next
		&& gelic_net_get_descr_status(card->tx_chain.head) ==
		     GELIC_NET_DESCR_NOT_IN_USE ) {
		return card->tx_chain.head;
	} else {
		return NULL;
	}
  } else {
	if ( card->tx_chain.tail != card->tx_chain.head->next
		&& card->tx_chain.tail != card->tx_chain.head->next->next
		&& gelic_net_get_descr_status(card->tx_chain.head) ==
		     GELIC_NET_DESCR_NOT_IN_USE
		&& gelic_net_get_descr_status(card->tx_chain.head->next) ==
		     GELIC_NET_DESCR_NOT_IN_USE ) {
		return card->tx_chain.head;
	} else {
		return NULL;
	}
  }
}

/**
 * gelic_net_set_txdescr_cmdstat - sets the tx descriptor command field
 * @descr: descriptor structure to fill out
 * @skb: packet to consider
 * @middle: middle of frame
 *
 * fills out the command and status field of the descriptor structure,
 * depending on hardware checksum settings. This function assumes a wmb()
 * has executed before.
 */
static void
gelic_net_set_txdescr_cmdstat(struct gelic_net_descr *descr,
			       struct sk_buff *skb, int middle)
{
	uint32_t nocs, tcpcs, udpcs;

	if (middle) {
		nocs =  GELIC_NET_DMAC_CMDSTAT_NOCS;
		tcpcs = GELIC_NET_DMAC_CMDSTAT_TCPCS;
		udpcs = GELIC_NET_DMAC_CMDSTAT_UDPCS;
	}else {
		nocs =  GELIC_NET_DMAC_CMDSTAT_NOCS
			| GELIC_NET_DMAC_CMDSTAT_END_FRAME;
		tcpcs = GELIC_NET_DMAC_CMDSTAT_TCPCS
			| GELIC_NET_DMAC_CMDSTAT_END_FRAME;
		udpcs = GELIC_NET_DMAC_CMDSTAT_UDPCS
			| GELIC_NET_DMAC_CMDSTAT_END_FRAME;
	}

	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		descr->dmac_cmd_status = nocs;
	} else {
		/* is packet ip?
		 * if yes: tcp? udp? */
		if (skb->protocol == htons(ETH_P_IP)) {
			if (skb->nh.iph->protocol == IPPROTO_TCP) {
				descr->dmac_cmd_status = tcpcs;
			} else if (skb->nh.iph->protocol == IPPROTO_UDP) {
				descr->dmac_cmd_status = udpcs;
			} else { /* the stack should checksum non-tcp and non-udp
				    packets on his own: NETIF_F_IP_CSUM */
				descr->dmac_cmd_status = nocs;
			}
		}
	}
}

/**
 * gelic_net_prepare_tx_descr - get dma address of skb_data
 * @card: card structure
 * @descr: descriptor structure
 * @skb: packet to use
 *
 * returns 0 on success, <0 on failure.
 *
 */
static int
gelic_net_prepare_tx_descr_v(struct gelic_net_card *card,
			    struct gelic_net_descr *descr,
			    struct sk_buff *skb)
{
	dma_addr_t buf;
	uint8_t *hdr;
	struct vlan_ethhdr *v_hdr;
	int vlan_len;

	if (skb->len < GELIC_NET_VLAN_POS) {
		printk("error: skb->len:%d\n", skb->len);
		return -EINVAL;
	}
	hdr = skb->data;
	v_hdr = (struct vlan_ethhdr *)skb->data;
	memcpy(&descr->vlan, v_hdr, GELIC_NET_VLAN_POS);
	if (card->vlan_index != -1) {
		descr->vlan.h_vlan_proto = htons(ETH_P_8021Q); /* vlan 0x8100*/
		descr->vlan.h_vlan_TCI = htons(card->vlan_id[card->vlan_index]);
		vlan_len = GELIC_NET_VLAN_POS + VLAN_HLEN; /* VLAN_HLEN=4 */
	} else {
		vlan_len = GELIC_NET_VLAN_POS; /* no vlan tag */
	}

	/* first descr */
	buf = dma_map_single(&card->dev->core, &descr->vlan,
					 vlan_len, PCI_DMA_BIDIRECTIONAL);

	if (buf == DMA_ERROR_CODE) {
		printk("could not iommu-map packet (%p, %i). "
			  "Dropping packet\n", v_hdr, vlan_len);
		return -ENOMEM;
	}

	descr->buf_addr = buf;
	descr->buf_size = vlan_len;
	descr->skb = skb; /* not used */
	descr->data_status = 0;
	gelic_net_set_txdescr_cmdstat(descr, skb, 1); /* not the frame end */

	/* second descr */
	card->tx_chain.head = card->tx_chain.head->next;
	descr->next_descr_addr = descr->next->bus_addr;
	descr = descr->next;
	if (gelic_net_get_descr_status(descr) !=
			GELIC_NET_DESCR_NOT_IN_USE) {
		printk("ERROR descr()\n"); /* XXX will be removed */
	}
	buf = dma_map_single(&card->dev->core, hdr + GELIC_NET_VLAN_POS,
				skb->len - GELIC_NET_VLAN_POS,
				PCI_DMA_BIDIRECTIONAL);

	if (buf == DMA_ERROR_CODE) {
		printk("could not iommu-map packet (%p, %i). "
			  "Dropping packet\n", hdr + GELIC_NET_VLAN_POS,
			  skb->len - GELIC_NET_VLAN_POS);
		return -ENOMEM;
	}

	descr->buf_addr = buf;
	descr->buf_size = skb->len - GELIC_NET_VLAN_POS;
	descr->skb = skb;
	descr->data_status = 0;
	descr->next_descr_addr= 0;
	gelic_net_set_txdescr_cmdstat(descr,skb, 0);

	return 0;
}

static int
gelic_net_prepare_tx_descr(struct gelic_net_card *card,
			    struct gelic_net_descr *descr,
			    struct sk_buff *skb)
{
	dma_addr_t buf = dma_map_single(&card->dev->core, skb->data,
					 skb->len, PCI_DMA_BIDIRECTIONAL);

	if (buf == DMA_ERROR_CODE) {
		printk("could not iommu-map packet (%p, %i). "
			  "Dropping packet\n", skb->data, skb->len);
		return -ENOMEM;
	}

	descr->buf_addr = buf;
	descr->buf_size = skb->len;
	descr->skb = skb;
	descr->data_status = 0;

	return 0;
}

static void
gelic_net_set_frame_end(struct gelic_net_card *card,
		struct gelic_net_descr *descr, struct sk_buff *skb)
{
	descr->next_descr_addr= 0;
	gelic_net_set_txdescr_cmdstat(descr,skb, 0);
	wmb();
	if (descr->prev) {
		descr->prev->next_descr_addr = descr->bus_addr;
	}
}

/**
 * gelic_net_kick_txdma - enables TX DMA processing
 * @card: card structure
 * @descr: descriptor address to enable TX processing at
 *
 */
static void
gelic_net_kick_txdma(struct gelic_net_card *card,
		       struct gelic_net_descr *descr)
{
	uint64_t status = -1;
	int count = 10;

	if (dmac_status) {
		return ;
	}

	if (gelic_net_get_descr_status(descr) == GELIC_NET_DESCR_CARDOWNED) {
		/* kick */
		dmac_status = 1;

		while(count--) {
			status = lv1_net_start_tx_dma(card->dev->did.bus_id,
					card->dev->did.dev_id,
					(uint64_t)descr->bus_addr, 0);
			if (!status) {
				break;
			}
		}
		if (!count) {
			printk("lv1_net_start_txdma failed, status=%ld %016lx\n",\
				status, gelic_irq_status);
		}
	}
}

/**
 * gelic_net_xmit - transmits a frame over the device
 * @skb: packet to send out
 * @netdev: interface device structure
 *
 * returns 0 on success, <0 on failure
 */
static int
gelic_net_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct gelic_net_card *card = netdev_priv(netdev);
	struct gelic_net_descr *descr = NULL;
	int result;
	unsigned long flags;

	spin_lock_irqsave(&card->intmask_lock, flags);

	gelic_net_release_tx_chain(card, 0);
	if (skb == NULL){
		goto kick;
	}
	descr = gelic_net_get_next_tx_descr(card); /* get tx_chain.head */
	if (!descr) {
		netif_stop_queue(netdev);
		spin_unlock_irqrestore(&card->intmask_lock, flags);
		return 1;
	}
  if (!ps3_gelic_param) {
	result = gelic_net_prepare_tx_descr(card, descr, skb);
  } else {
	result = gelic_net_prepare_tx_descr_v(card, descr, skb);
  }
	if (result)
		goto error;

	card->tx_chain.head = card->tx_chain.head->next;
  if (!ps3_gelic_param) {
	gelic_net_set_frame_end(card, descr, skb);
  } else {
	if (descr->prev) {
		descr->prev->next_descr_addr = descr->bus_addr;
	}
  }
kick:
	wmb();
	gelic_net_kick_txdma(card, card->tx_chain.tail);

	netdev->trans_start = jiffies;
	spin_unlock_irqrestore(&card->intmask_lock, flags);
	return NETDEV_TX_OK;

error:
	card->netdev_stats.tx_dropped++;
	spin_unlock_irqrestore(&card->intmask_lock, flags);
	return NETDEV_TX_LOCKED;
}

/**
 * gelic_net_pass_skb_up - takes an skb from a descriptor and passes it on
 * @descr: descriptor to process
 * @card: card structure
 *
 * returns 1 on success, 0 if no packet was passed to the stack
 *
 * iommu-unmaps the skb, fills out skb structure and passes the data to the
 * stack. The descriptor state is not changed.
 */
static int
gelic_net_pass_skb_up(struct gelic_net_descr *descr,
		       struct gelic_net_card *card)
{
	struct sk_buff *skb;
	struct net_device *netdev;
	uint32_t data_status, data_error;

	data_status = descr->data_status;
	data_error = descr->data_error;

	netdev = card->netdev;
	/* check for errors in the data_error flag */
	if ((data_error & GELIC_NET_DATA_ERROR_MASK))
		DPRINTK("error in received descriptor found, "
		       "data_status=x%08x, data_error=x%08x\n",
		       data_status, data_error);
	/* prepare skb, unmap descriptor */
	skb = descr->skb;
	dma_unmap_single(&card->dev->core, descr->buf_addr, GELIC_NET_MAX_MTU,
			 PCI_DMA_BIDIRECTIONAL);

	/* the cases we'll throw away the packet immediately */
	if (data_error & GELIC_NET_DATA_ERROR_FLG) {
		DPRINTK("ERROR DESTROY:%x\n", data_error);
		return 0;
	}

	skb->dev = netdev;
	skb_put(skb, descr->valid_size);
	descr->skb = NULL;
	/* the card seems to add 2 bytes of junk in front
	 * of the ethernet frame */
#define GELIC_NET_MISALIGN		2
	skb_pull(skb, GELIC_NET_MISALIGN);
	skb->protocol = eth_type_trans(skb, netdev);

	/* checksum offload */
	if (card->rx_csum) {
		if ( (data_status & GELIC_NET_DATA_STATUS_CHK_MASK) &&
		     (!(data_error & GELIC_NET_DATA_ERROR_CHK_MASK)) )
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = CHECKSUM_NONE;
	} else {
		skb->ip_summed = CHECKSUM_NONE;
	}

	/* pass skb up to stack */
	netif_receive_skb(skb);

	/* update netdevice statistics */
	card->netdev_stats.rx_packets++;
	card->netdev_stats.rx_bytes += skb->len;

	return 1;
}

/**
 * gelic_net_decode_descr - processes an rx descriptor
 * @card: card structure
 *
 * returns 1 if a packet has been sent to the stack, otherwise 0
 *
 * processes an rx descriptor by iommu-unmapping the data buffer and passing
 * the packet up to the stack
 */
static int
gelic_net_decode_one_descr(struct gelic_net_card *card)
{
	enum gelic_net_descr_status status;
	struct gelic_net_descr *descr;
	struct gelic_net_descr_chain *chain = &card->rx_chain;
	int result = 0;
	int kick = 0;
	uint32_t cmd_status;

	descr = chain->tail;
	cmd_status = chain->tail->dmac_cmd_status;
	rmb();
	status = cmd_status >> GELIC_NET_DESCR_IND_PROC_SHIFT;
	if (status == GELIC_NET_DESCR_CARDOWNED) {
		goto no_decode;
	}
	if (status == GELIC_NET_DESCR_NOT_IN_USE) {
		printk("err: decode_one_descr\n");
		goto no_decode;
	}

	if ( (status == GELIC_NET_DESCR_RESPONSE_ERROR) ||
	     (status == GELIC_NET_DESCR_PROTECTION_ERROR) ||
	     (status == GELIC_NET_DESCR_FORCE_END) ) {
		printk("%s: dropping RX descriptor with state %d\n",
		       card->netdev->name, status);
		card->netdev_stats.rx_dropped++;
		goto refill;
	}

	if ( (status != GELIC_NET_DESCR_COMPLETE) &&
	     (status != GELIC_NET_DESCR_FRAME_END) ) {
		printk("%s: RX descriptor with state %d\n",
		       card->netdev->name, status);
		goto refill;
	}

	/* ok, we've got a packet in descr */
	result = gelic_net_pass_skb_up(descr, card); /* 1: skb_up sccess */
	if (cmd_status & GELIC_NET_DMAC_CMDSTAT_CHAIN_END) {
		kick = 1;
	}
refill:
	descr->next_descr_addr = 0; /* unlink the descr */
	wmb();
	gelic_net_set_descr_status(descr, GELIC_NET_DESCR_NOT_IN_USE);
	/* change the descriptor state: */
	gelic_net_prepare_rx_descr(card, descr); /* refill one desc */
	chain->head = descr;
	chain->tail = descr->next;
	descr->prev->next_descr_addr = descr->bus_addr;
	if(kick) {
		wmb();
		gelic_net_enable_rxdmac(card);
	}
	return result;

no_decode:
	return 0;
}

/**
 * gelic_net_poll - NAPI poll function called by the stack to return packets
 * @netdev: interface device structure
 * @budget: number of packets we can pass to the stack at most
 *
 * returns 0 if no more packets available to the driver/stack. Returns 1,
 * if the quota is exceeded, but the driver has still packets.
 *
 */
static int
gelic_net_poll(struct net_device *netdev, int *budget)
{
	struct gelic_net_card *card = netdev_priv(netdev);
	int packets_to_do, packets_done = 0;
	int no_more_packets = 0;

	packets_to_do = min(*budget, netdev->quota);

	while (packets_to_do) {
		if (gelic_net_decode_one_descr(card)) {
			packets_done++;
			packets_to_do--;
		} else {
			/* no more packets for the stack */
			no_more_packets = 1;
			break;
		}
	}
	netdev->quota -= packets_done;
	*budget -= packets_done;
	if (no_more_packets == 1) {
		netif_rx_complete(netdev);

		/* one more check */
		while (1) {
			if (!gelic_net_decode_one_descr(card) ) break;
		};

		return 0;
	}else {
		return 1;
	}
}

/**
 * gelic_net_get_stats - get interface statistics
 * @netdev: interface device structure
 *
 * returns the interface statistics residing in the gelic_net_card struct
 */
static struct net_device_stats *
gelic_net_get_stats(struct net_device *netdev)
{
	struct gelic_net_card *card = netdev_priv(netdev);
	struct net_device_stats *stats = &card->netdev_stats;

	return stats;
}

/**
 * gelic_net_change_mtu - changes the MTU of an interface
 * @netdev: interface device structure
 * @new_mtu: new MTU value
 *
 * returns 0 on success, <0 on failure
 */
static int
gelic_net_change_mtu(struct net_device *netdev, int new_mtu)
{
	/* no need to re-alloc skbs or so -- the max mtu is about 2.3k
	 * and mtu is outbound only anyway */
	if ( (new_mtu < GELIC_NET_MIN_MTU ) ||
		(new_mtu > GELIC_NET_MAX_MTU) ) {
		return -EINVAL;
	}
	netdev->mtu = new_mtu;
	return 0;
}

/**
 * gelic_net_interrupt - event handler for gelic_net
 */
static irqreturn_t
gelic_net_interrupt(int irq, void *ptr)
{
	struct net_device *netdev = ptr;
	struct gelic_net_card *card = netdev_priv(netdev);
	uint32_t status0, status1, status2;
	unsigned long flags;
	uint64_t status;

	status = gelic_irq_status;
	rmb();
	status0 = (uint32_t)(status >> 32);
	status1 = (uint32_t)(status & 0xffffffff);
	status2 = 0;

	if (!status0 && !status1 && !status2) {
		return IRQ_NONE;
	}

	if(status1 & (1 << GELIC_NET_GDADCEINT) )  {
		netif_rx_schedule(netdev);
	}else
	if (status0 & (1 << GELIC_NET_GRFANMINT) ) {
		netif_rx_schedule(netdev);
	}

	if (status0 & (1 << GELIC_NET_GDTDCEINT) ) {
		spin_lock_irqsave(&card->intmask_lock, flags);
		dmac_status = 0;
		spin_unlock_irqrestore(&card->intmask_lock, flags);
		gelic_net_xmit(NULL, netdev);
	}
	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * gelic_net_poll_controller - artificial interrupt for netconsole etc.
 * @netdev: interface device structure
 *
 * see Documentation/networking/netconsole.txt
 */
static void
gelic_net_poll_controller(struct net_device *netdev)
{
	struct gelic_net_card *card = netdev_priv(netdev);

	gelic_net_set_irq_mask(card, 0);
	gelic_net_interrupt(netdev->irq, netdev);
	gelic_net_set_irq_mask(card, card->ghiintmask);
}
#endif /* CONFIG_NET_POLL_CONTROLLER */

/**
 * gelic_net_open_device - open device and map dma region
 * @card: card structure
 */
static int
gelic_net_open_device(struct gelic_net_card *card)
{
	unsigned long result;
	int ret;

	result = ps3_sb_event_receive_port_setup(PS3_BINDING_CPU_ANY,
		&card->dev->did, card->dev->interrupt_id, &card->netdev->irq);

	if (result) {
		printk("%s:%d: gelic_net_open_device failed (%ld)\n",
			__func__, __LINE__, result);
		ret = -EPERM;
		goto fail_alloc_irq;
	}

	ret = request_irq(card->netdev->irq, gelic_net_interrupt, IRQF_DISABLED,
		"gelic network", card->netdev);

	if (ret) {
		printk("%s:%d: request_irq failed (%ld)\n",
			__func__, __LINE__, result);
		goto fail_request_irq;
	}

	return 0;

fail_request_irq:
	ps3_sb_event_receive_port_destroy(&card->dev->did,
		card->dev->interrupt_id, card->netdev->irq);
	card->netdev->irq = NO_IRQ;
fail_alloc_irq:
	return ret;
}


/**
 * gelic_net_open - called upon ifonfig up
 * @netdev: interface device structure
 *
 * returns 0 on success, <0 on failure
 *
 * gelic_net_open allocates all the descriptors and memory needed for
 * operation, sets up multicast list and enables interrupts
 */
int
gelic_net_open(struct net_device *netdev)
{
	struct gelic_net_card *card = netdev_priv(netdev);

	printk(" -> %s:%d\n", __func__, __LINE__);

	gelic_net_open_device(card);

	if (gelic_net_init_chain(card, &card->tx_chain,
			card->descr, tx_descriptors))
		goto alloc_tx_failed;
	if (gelic_net_init_chain(card, &card->rx_chain,
			card->descr + tx_descriptors, rx_descriptors))
		goto alloc_rx_failed;

	/* head of chain */
	card->tx_top = card->tx_chain.head;
	card->rx_top = card->rx_chain.head;

	/* allocate rx skbs */
	if (gelic_net_alloc_rx_skbs(card))
		goto alloc_skbs_failed;

	dmac_status = 0;
	card->ghiintmask = GELIC_NET_RXINT | GELIC_NET_TXINT;
	gelic_net_set_irq_mask(card, card->ghiintmask);
	gelic_net_enable_rxdmac(card);

	netif_start_queue(netdev);
	netif_carrier_on(netdev);
	netif_poll_enable(netdev);

	return 0;

alloc_skbs_failed:
	gelic_net_free_chain(card, card->rx_top);
alloc_rx_failed:
	gelic_net_free_chain(card, card->tx_top);
alloc_tx_failed:
	return -ENOMEM;
}

#ifdef GELIC_NET_ETHTOOL
static void
gelic_net_get_drvinfo (struct net_device *netdev, struct ethtool_drvinfo *info)
{
	strncpy(info->driver, GELIC_NET_DRV_NAME, sizeof(info->driver) - 1);
	strncpy(info->version, GELIC_NET_DRV_VERSION, sizeof(info->version) - 1);
}

static int
gelic_net_get_settings(struct net_device *netdev, struct ethtool_cmd *cmd)
{
	struct gelic_net_card *card = netdev_priv(netdev);
	uint64_t status, v1, v2;
	int speed, duplex;

	speed = duplex = -1;
	status = lv1_net_control(card->dev->did.bus_id, card->dev->did.dev_id,
			GELIC_NET_GET_ETH_PORT_STATUS, GELIC_NET_PORT, 0, 0,
			&v1, &v2);
	if (status) {
		/* link down */
	} else {
		if (v1 & GELIC_NET_FULL_DUPLEX) {
			duplex = DUPLEX_FULL;
		} else {
			duplex = DUPLEX_HALF;
		}

		if (v1 & GELIC_NET_SPEED_10 ) {
			speed = SPEED_10;
		} else if (v1 & GELIC_NET_SPEED_100) {
			speed = SPEED_100;
		} else if (v1 & GELIC_NET_SPEED_1000) {
			speed = SPEED_1000;
		}
	}
	cmd->supported = SUPPORTED_TP | SUPPORTED_Autoneg |
			SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full |
			SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full |
			SUPPORTED_1000baseT_Half | SUPPORTED_1000baseT_Full;
	cmd->advertising = cmd->supported;
	cmd->speed = speed;
	cmd->duplex = duplex;
	cmd->autoneg = AUTONEG_ENABLE; /* always enabled */
	cmd->port = PORT_TP;

	return 0;
}

static uint32_t
gelic_net_get_link(struct net_device *netdev)
{
	struct gelic_net_card *card = netdev_priv(netdev);
	uint64_t status, v1, v2;
	int link;

	status = lv1_net_control(card->dev->did.bus_id, card->dev->did.dev_id,
			GELIC_NET_GET_ETH_PORT_STATUS, GELIC_NET_PORT, 0, 0,
			&v1, &v2);
	if (status) {
		return 0; /* link down */
	}
	if (v1 & GELIC_NET_LINK_UP)
		link = 1;
	else
		link = 0;
	return link;
}

static int
gelic_net_nway_reset(struct net_device *netdev)
{
	if (netif_running(netdev)) {
		gelic_net_stop(netdev);
		gelic_net_open(netdev);
	}
	return 0;
}

static uint32_t
gelic_net_get_tx_csum(struct net_device *netdev)
{
	return (netdev->features & NETIF_F_IP_CSUM) != 0;
}

static int
gelic_net_set_tx_csum(struct net_device *netdev, uint32_t data)
{
	if (data)
		netdev->features |= NETIF_F_IP_CSUM;
	else
		netdev->features &= ~NETIF_F_IP_CSUM;

	return 0;
}

static uint32_t
gelic_net_get_rx_csum(struct net_device *netdev)
{
	struct gelic_net_card *card = netdev_priv(netdev);

	return card->rx_csum;
}

static int
gelic_net_set_rx_csum(struct net_device *netdev, uint32_t data)
{
	struct gelic_net_card *card = netdev_priv(netdev);

	card->rx_csum = data;
	return 0;
}

static struct ethtool_ops gelic_net_ethtool_ops = {
	.get_drvinfo	= gelic_net_get_drvinfo,
	.get_settings	= gelic_net_get_settings,
	.get_link	= gelic_net_get_link,
	.nway_reset	= gelic_net_nway_reset,
	.get_tx_csum	= gelic_net_get_tx_csum,
	.set_tx_csum	= gelic_net_set_tx_csum,
	.get_rx_csum	= gelic_net_get_rx_csum,
	.set_rx_csum	= gelic_net_set_rx_csum,
};
#endif

static int
gelic_net_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct gelic_net_card *card = netdev_priv(netdev);
	void __user *addr = (void __user *) ifr->ifr_ifru.ifru_data;
	int mode, res = 0;

	switch(cmd) {
	case GELIC_NET_GET_MODE:
		DPRINTK("GELIC_NET_GET_MODE:\n");
		mode = card->vlan_index;
		if (copy_to_user(addr, &mode, sizeof(mode)) ) {
			printk("error copy_to_user\n");
		}
		res = 0;
		break;
	case GELIC_NET_SET_MODE:
		if (card->vlan_index == -1) {
			res = -EOPNOTSUPP; /* vlan mode only */
			break;
		}
		if (copy_from_user(&mode, addr, sizeof(mode)) ) {
			printk("error copy_from_user\n");
		}
		DPRINTK("GELIC_NET_SET_MODE:%x --> %x \n",
				card->vlan_index, mode);
		if (mode > GELIC_NET_VLAN_MAX -1 || mode < -1)
			mode = GELIC_NET_VLAN_WIRED - 1;

		if (card->vlan_index != mode) {
			card->vlan_index = mode;
			if (netif_running(netdev)) {
				gelic_net_stop(netdev);
				gelic_net_open(netdev);
			}
		}
		res = 0;
		break;
	default:
		res = -EOPNOTSUPP;
		break;
	}

	return res;
}

/**
 * gelic_net_tx_timeout_task - task scheduled by the watchdog timeout
 * function (to be called not under interrupt status)
 * @data: data, is interface device structure
 *
 * called as task when tx hangs, resets interface (if interface is up)
 */
static void
gelic_net_tx_timeout_task(struct work_struct *work)
{
	struct gelic_net_card *card =
		container_of(work, struct gelic_net_card, tx_timeout_task);
	struct net_device *netdev = card->netdev;

	printk("Timed out. Restarting... \n");

	if (!(netdev->flags & IFF_UP))
		goto out;

	netif_device_detach(netdev);
	gelic_net_stop(netdev);

	gelic_net_open(netdev);
	netif_device_attach(netdev);

out:
	atomic_dec(&card->tx_timeout_task_counter);
}

/**
 * gelic_net_tx_timeout - called when the tx timeout watchdog kicks in.
 * @netdev: interface device structure
 *
 * called, if tx hangs. Schedules a task that resets the interface
 */
static void
gelic_net_tx_timeout(struct net_device *netdev)
{
	struct gelic_net_card *card;

	card = netdev_priv(netdev);
	atomic_inc(&card->tx_timeout_task_counter);
	if (netdev->flags & IFF_UP)
		schedule_work(&card->tx_timeout_task);
	else
		atomic_dec(&card->tx_timeout_task_counter);
}

/**
 * gelic_net_setup_netdev_ops - initialization of net_device operations
 * @netdev: net_device structure
 *
 * fills out function pointers in the net_device structure
 */
static void
gelic_net_setup_netdev_ops(struct net_device *netdev)
{
	netdev->open = &gelic_net_open;
	netdev->stop = &gelic_net_stop;
	netdev->hard_start_xmit = &gelic_net_xmit;
	netdev->get_stats = &gelic_net_get_stats;
	netdev->set_multicast_list = &gelic_net_set_multi;
	netdev->change_mtu = &gelic_net_change_mtu;
	/* tx watchdog */
	netdev->tx_timeout = &gelic_net_tx_timeout;
	netdev->watchdog_timeo = GELIC_NET_WATCHDOG_TIMEOUT;
	/* NAPI */
	netdev->poll = &gelic_net_poll;
	netdev->weight = GELIC_NET_NAPI_WEIGHT;
#ifdef GELIC_NET_ETHTOOL
	netdev->ethtool_ops = &gelic_net_ethtool_ops;
#endif
	netdev->do_ioctl = &gelic_net_ioctl;
}

/**
 * gelic_net_setup_netdev - initialization of net_device
 * @card: card structure
 *
 * Returns 0 on success or <0 on failure
 *
 * gelic_net_setup_netdev initializes the net_device structure
 **/
static int
gelic_net_setup_netdev(struct gelic_net_card *card)
{
	int i, result;
	struct net_device *netdev = card->netdev;
	struct sockaddr addr;
	uint8_t *mac;
	uint64_t status, v1, v2;

	SET_MODULE_OWNER(netdev);
	spin_lock_init(&card->intmask_lock);

	card->rx_csum = GELIC_NET_RX_CSUM_DEFAULT;

	gelic_net_setup_netdev_ops(netdev);

	netdev->features = NETIF_F_IP_CSUM;

	status = lv1_net_control(card->dev->did.bus_id, card->dev->did.dev_id,
				GELIC_NET_GET_MAC_ADDRESS,
				0, 0, 0, &v1, &v2);
	if (status || !v1) {
		printk("lv1_net_control GET_MAC_ADDR not supported, status=%ld\n",
			status);
		return -EINVAL;
	}
	v1 <<= 16;
	mac = (uint8_t *)&v1;
	memcpy(addr.sa_data, mac, ETH_ALEN);
	memcpy(netdev->dev_addr, addr.sa_data, ETH_ALEN);

	result = register_netdev(netdev);
	if (result) {
			printk("Couldn't register net_device: %i\n", result);
		return result;
	}

	printk("%s: %s\n", netdev->name, GELIC_NET_DRV_NAME);
	printk("%s: Ethernet Address: %2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X\n",
		netdev->name,
		netdev->dev_addr[0], netdev->dev_addr[1], netdev->dev_addr[2],
		netdev->dev_addr[3], netdev->dev_addr[4], netdev->dev_addr[5]);

	card->vlan_index = -1;	/* no vlan */
	for (i = 0; i < GELIC_NET_VLAN_MAX ;i++) {
		status = lv1_net_control(card->dev->did.bus_id,
					card->dev->did.dev_id,
					GELIC_NET_GET_VLAN_ID,
					i + 1, /* GELIC_NET_VLAN_X */
					0, 0, &v1, &v2);
		if (status == GELIC_NET_VLAN_NO_ENTRY) {
			DPRINTK("GELIC_VLAN_ID no entry:%ld, VLAN disabled\n",
				status);
			card->vlan_id[i] = 0;
		} else if (status) {
			printk("GELIC_NET_VLAN_ID faild, status=%ld\n", status);
			card->vlan_id[i] = 0;
		} else {
			card->vlan_id[i] = (uint32_t)v1;
			DPRINTK("vlan_id:%d, %lx\n", i, v1);
		}
	}
	if (card->vlan_id[GELIC_NET_VLAN_WIRED - 1]) {
		card->vlan_index = GELIC_NET_VLAN_WIRED - 1;
	}
	return 0;
}

/**
 * gelic_net_alloc_card - allocates net_device and card structure
 *
 * returns the card structure or NULL in case of errors
 *
 * the card and net_device structures are linked to each other
 */
static struct gelic_net_card *
gelic_net_alloc_card(void)
{
	struct net_device *netdev;
	struct gelic_net_card *card;
	size_t alloc_size;

	alloc_size = sizeof (*card) +
		sizeof (struct gelic_net_descr) * rx_descriptors +
		sizeof (struct gelic_net_descr) * tx_descriptors;
	netdev = alloc_etherdev(alloc_size);
	if (!netdev)
		return NULL;

	card = netdev_priv(netdev);
	card->netdev = netdev;
	INIT_WORK(&card->tx_timeout_task, gelic_net_tx_timeout_task);
	init_waitqueue_head(&card->waitq);
	atomic_set(&card->tx_timeout_task_counter, 0);

	return card;
}

/**
 * ps3_gelic_driver_probe - add a device to the control of this driver
 */
static int ps3_gelic_driver_probe (struct ps3_system_bus_device *dev)
{
	struct gelic_net_card *card;
	int error = -EIO;
	uint64_t status;
	uint64_t lpar;

	card = gelic_net_alloc_card();
	if (!card) {
		printk("Couldn't allocate net_device structure, aborting.\n");
		return -ENOMEM;
	}
	gcard = card;
	card->dev = dev;

	/* setup status indicator */
	lpar = ps3_mm_phys_to_lpar(__pa(&gelic_irq_status));
	status = lv1_net_set_interrupt_status_indicator(
						card->dev->did.bus_id,
						card->dev->did.dev_id,
						lpar, 0);
	if (status) {
		printk("lv1_net_set_interrupt_status_indicator failed, status=%ld\n",
			status);
		goto error;
	}

	error = gelic_net_setup_netdev(card);
	if (error) {
		printk("gelic_net_setup_netdev() failed: error = %d\n", error);
		goto error;
	}
	return 0;

error:
	free_netdev(card->netdev);
	return error;
}

/**
 * ps3_gelic_driver_remove - remove a device from the control of this driver
 */

static int
ps3_gelic_driver_remove (struct ps3_system_bus_device *dev)
{
	struct net_device *netdev;
	struct gelic_net_card *card;

	card = gcard;
	netdev = card->netdev;

	wait_event(card->waitq,
		   atomic_read(&card->tx_timeout_task_counter) == 0);

	unregister_netdev(netdev);
	free_netdev(netdev);

	return 0;
}

static struct ps3_system_bus_driver ps3_gelic_driver = {
	.match_id = PS3_MATCH_ID_GELIC,
	.probe = ps3_gelic_driver_probe,
	.remove = ps3_gelic_driver_remove,
	.core = {
		.name = "ps3_gelic_driver",
	},
};

static int __init
ps3_gelic_driver_init (void)
{
	return ps3_system_bus_driver_register(&ps3_gelic_driver);
}

static void __exit
ps3_gelic_driver_exit (void)
{
	ps3_system_bus_driver_unregister(&ps3_gelic_driver);
}

module_init (ps3_gelic_driver_init);
module_exit (ps3_gelic_driver_exit);

#ifdef CONFIG_GELIC_NET
static int __init early_param_gelic_net(char *p)
{
	if (strstr(p, "n")) {
		ps3_gelic_param = 0;	/* gelic_vlan off */
		printk("ps3_gelic_param:vlan off\n");
	} else {
		ps3_gelic_param = 1;	/* gelic_vlan on */
	}
	return 0;

}
early_param("gelic_vlan", early_param_gelic_net);
#endif
