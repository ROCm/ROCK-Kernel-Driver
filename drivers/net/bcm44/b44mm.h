
/******************************************************************************/
/*                                                                            */
/* Broadcom BCM4400 Linux Network Driver, Copyright (c) 2002 Broadcom         */
/* Corporation.                                                               */
/* All rights reserved.                                                       */
/*                                                                            */
/* This program is free software; you can redistribute it and/or modify       */
/* it under the terms of the GNU General Public License as published by       */
/* the Free Software Foundation, located in the file LICENSE.                 */
/*                                                                            */
/******************************************************************************/

#ifndef B44MM_H
#define B44MM_H

#include <linux/config.h>
#if defined(CONFIG_SMP) && ! defined(__SMP__)
#define __SMP__
#endif
#if defined(CONFIG_MODVERSIONS) && defined(MODULE) && ! defined(MODVERSIONS)
#define MODVERSIONS
#endif

#ifndef B44UM
#define __NO_VERSION__
#endif
#include <linux/version.h>
#ifdef MODULE
#if defined(MODVERSIONS) && (LINUX_VERSION_CODE < 0x020500)
#include <linux/modversions.h>
#endif
#include <linux/module.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#define SET_MODULE_OWNER(dev)
#define MODULE_DEVICE_TABLE(pci, pci_tbl)
#endif

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/unaligned.h>
#include <linux/delay.h>
#include <asm/byteorder.h>
#include <linux/time.h>
#if (LINUX_VERSION_CODE >= 0x020400)
#include <linux/ethtool.h>
#include <asm/uaccess.h>
#endif
#ifdef CONFIG_PROC_FS
#include <linux/smp_lock.h>
#include <linux/proc_fs.h>
#define BCM_PROC_FS 1
#endif

#define BCM_WOL 1

#ifdef __BIG_ENDIAN
#define BIG_ENDIAN_HOST 1
#endif

#if HAVE_NETIF_RECEIVE_SKB
#define BCM_NAPI_RXPOLL 1
#undef BCM_TASKLET
#endif

#define MM_SWAP_LE16(x) cpu_to_le16(x)

#if (LINUX_VERSION_CODE < 0x020327)
#define __raw_readl readl
#define __raw_writel writel
#endif

#define MM_MEMWRITEL(ptr, val) __raw_writel(val, ptr)
#define MM_MEMREADL(ptr) __raw_readl(ptr)

typedef atomic_t MM_ATOMIC_T;

#define MM_ATOMIC_SET(ptr, val) atomic_set(ptr, val)
#define MM_ATOMIC_READ(ptr) atomic_read(ptr)
#define MM_ATOMIC_INC(ptr) atomic_inc(ptr)
#define MM_ATOMIC_ADD(ptr, val) atomic_add(val, ptr)
#define MM_ATOMIC_DEC(ptr) atomic_dec(ptr)
#define MM_ATOMIC_SUB(ptr, val) atomic_sub(val, ptr)

#define MM_MB() mb()
#define MM_WMB() wmb()

#include "b44lm.h"
#include "b44queue.h"
#include "b44.h"

#if DBG
#define STATIC
#else
#define STATIC static
#endif

extern int b44_Packet_Desc_Size;

#define B44_MM_PACKET_DESC_SIZE b44_Packet_Desc_Size

DECLARE_QUEUE_TYPE(UM_RX_PACKET_Q, MAX_RX_PACKET_DESC_COUNT+1);

#define MAX_MEM 16

#if (LINUX_VERSION_CODE < 0x020211)
typedef u32 dma_addr_t;
#endif

#if (LINUX_VERSION_CODE < 0x02032a)
#define pci_map_single(dev, address, size, dir) virt_to_bus(address)
#define pci_unmap_single(dev, dma_addr, size, dir)
#endif

#if MAX_SKB_FRAGS
#if (LINUX_VERSION_CODE >= 0x02040d)

typedef dma_addr_t dmaaddr_high_t;

#else

#if defined(CONFIG_HIGHMEM) && defined(CONFIG_X86)

#if defined(CONFIG_HIGHMEM64G)
typedef unsigned long long dmaaddr_high_t;
#else
typedef dma_addr_t dmaaddr_high_t;
#endif

#ifndef pci_map_page
#define pci_map_page bcm_pci_map_page
#endif

static inline dmaaddr_high_t
bcm_pci_map_page(struct pci_dev *dev, struct page *page,
		    int offset, size_t size, int dir)
{
	dmaaddr_high_t phys;

	phys = (page-mem_map) *	(dmaaddr_high_t) PAGE_SIZE + offset;

	return phys;
}

#ifndef pci_unmap_page
#define pci_unmap_page(dev, map, size, dir)
#endif

#else /* #if defined(CONFIG_HIGHMEM) && defined(CONFIG_X86) */

typedef dma_addr_t dmaaddr_high_t;

/* Warning - This may not work for all architectures if HIGHMEM is defined */

#ifndef pci_map_page
#define pci_map_page(dev, page, offset, size, dir) \
	pci_map_single(dev, page_address(page) + (offset), size, dir)
#endif
#ifndef pci_unmap_page
#define pci_unmap_page(dev, map, size, dir) \
	pci_unmap_single(dev, map, size, dir)
#endif

#endif /* #if defined(CONFIG_HIGHMEM) && defined(CONFIG_X86)*/

#endif /* #if (LINUX_VERSION_CODE >= 0x02040d)*/
#endif /* #if MAX_SKB_FRAGS*/

#if defined (CONFIG_X86)
#define NO_PCI_UNMAP 1
#endif

#if (LINUX_VERSION_CODE < 0x020412)
#if ! defined (NO_PCI_UNMAP)
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME) dma_addr_t ADDR_NAME;
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME) __u32 LEN_NAME;

#define pci_unmap_addr(PTR, ADDR_NAME)	\
	((PTR)->ADDR_NAME)

#define pci_unmap_len(PTR, LEN_NAME)	\
	((PTR)->LEN_NAME)

#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)	\
	(((PTR)->ADDR_NAME) = (VAL))

#define pci_unmap_len_set(PTR, LEN_NAME, VAL)	\
	(((PTR)->LEN_NAME) = (VAL))
#else
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)
#define DECLARE_PCI_UNMAP_LEN(ADDR_NAME)

#define pci_unmap_addr(PTR, ADDR_NAME)	0
#define pci_unmap_len(PTR, LEN_NAME)	0
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL) do { } while (0)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL) do { } while (0)
#endif
#endif

#if (LINUX_VERSION_CODE < 0x02030e)
#define net_device device
#define netif_carrier_on(dev)
#define netif_carrier_off(dev)
#endif

#if (LINUX_VERSION_CODE < 0x02032b)
#define tasklet_struct			tq_struct
#endif

typedef struct _UM_DEVICE_BLOCK {
	LM_DEVICE_BLOCK lm_dev;
	struct net_device *dev;
	struct pci_dev *pdev;
	struct net_device *next_module;
	char *name;
#ifdef BCM_PROC_FS
	struct proc_dir_entry *pfs_entry;
	char pfs_name[32];
#endif
	void *mem_list[MAX_MEM];
	dma_addr_t dma_list[MAX_MEM];
	int mem_size_list[MAX_MEM];
	int mem_list_num;
	int index;
	int opened;
	int delayed_link_ind; /* Delay link status during initial load */
	int timer_interval;
	int link_interval;
	int tx_full;
	int tx_queued;
	int line_speed;		/* in Mbps, 0 if link is down */
	UM_RX_PACKET_Q rx_out_of_buf_q;
	int rx_buf_repl_thresh;
	int rx_buf_repl_panic_thresh;
	struct timer_list timer;
	spinlock_t phy_lock;
	volatile unsigned long interrupt;
	atomic_t intr_sem;
	int tasklet_pending;
	volatile unsigned long tasklet_busy;
	struct tasklet_struct tasklet;
	struct net_device_stats stats;
	uint rx_misc_errors;
} UM_DEVICE_BLOCK, *PUM_DEVICE_BLOCK;

typedef struct _UM_PACKET {
	LM_PACKET lm_packet;
	struct sk_buff *skbuff;
#if MAX_SKB_FRAGS
	DECLARE_PCI_UNMAP_ADDR(map[MAX_SKB_FRAGS + 1])
	DECLARE_PCI_UNMAP_LEN(map_len[MAX_SKB_FRAGS + 1])
#else
	DECLARE_PCI_UNMAP_ADDR(map[1])
	DECLARE_PCI_UNMAP_LEN(map_len[1])
#endif
} UM_PACKET, *PUM_PACKET;


static inline void b44_MM_MapRxDma(PLM_DEVICE_BLOCK pDevice,
	struct _LM_PACKET *pPacket,
	LM_UINT32 *paddr)
{
	dma_addr_t map;

	map = pci_map_single(((struct _UM_DEVICE_BLOCK *)pDevice)->pdev,
			pPacket->u.Rx.pRxBufferVirt,
			pPacket->u.Rx.RxBufferSize,
			PCI_DMA_FROMDEVICE);
	pci_unmap_addr_set(((struct _UM_PACKET *) pPacket), map[0], map);
	*paddr = (LM_UINT32) map;
}

static inline void b44_MM_MapTxDma(PLM_DEVICE_BLOCK pDevice,
	struct _LM_PACKET *pPacket,
	LM_UINT32 *paddr, LM_UINT32 *len, int frag)
{
	dma_addr_t map;
	struct sk_buff *skb = ((struct _UM_PACKET *) pPacket)->skbuff;

	map = pci_map_single(((struct _UM_DEVICE_BLOCK *)pDevice)->pdev,
		skb->data,
		skb->len,
		PCI_DMA_TODEVICE);
	pci_unmap_addr_set(((struct _UM_PACKET *)pPacket), map[0], map);
	*paddr = (LM_UINT32) map;
	*len = skb->len;
}

#define B44_MM_PTR(_ptr)   ((unsigned long) (_ptr))

#if (BITS_PER_LONG == 64)
#define B44_MM_GETSTATS(_Ctr) \
	(unsigned long) (_Ctr).Low + ((unsigned long) (_Ctr).High << 32)
#else
#define B44_MM_GETSTATS(_Ctr) \
	(unsigned long) (_Ctr).Low
#endif


#define printf(fmt, args...) printk(KERN_DEBUG fmt, ##args)

#define DbgPrint(fmt, arg...) printk(KERN_WARNING fmt, ##arg)
#if defined(CONFIG_X86)
#define DbgBreakPoint() __asm__("int $129")
#else
#define DbgBreakPoint()
#endif
#define b44_MM_Wait(time) udelay(time)

#define ASSERT(expr)							\
	if (!(expr)) {							\
		printk(KERN_DEBUG "ASSERT failed: %s\n", #expr);	\
	}
#endif
