/**************************************************************************/
/*                                                                        */
/* IBM eServer i/pSeries Virtual Ethernet Device Driver                   */
/* Copyright (C) 2003 IBM Corp.                                           */
/*  Dave Larson (larson1@us.ibm.com)                                      */
/*  Santiago Leon (santil@us.ibm.com)                                     */
/*                                                                        */
/*  This program is free software; you can redistribute it and/or modify  */
/*  it under the terms of the GNU General Public License as published by  */
/*  the Free Software Foundation; either version 2 of the License, or     */
/*  (at your option) any later version.                                   */
/*                                                                        */
/*  This program is distributed in the hope that it will be useful,       */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/*  GNU General Public License for more details.                          */
/*                                                                        */
/*  You should have received a copy of the GNU General Public License     */
/*  along with this program; if not, write to the Free Software           */
/*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  */
/*                                                                   USA  */
/*                                                                        */
/* This module contains the implementation of a virtual ethernet device   */
/* for use with IBM i/pSeries LPAR Linux.  It utilizes the logical LAN    */
/* option of the RS/6000 Platform Architechture to interface with virtual */
/* ethernet NICs that are presented to the partition by the hypervisor.   */
/*                                                                        */ 
/**************************************************************************/
/*
   TODO:
     - remove frag processing code - no longer needed
     - add support for sysfs
     - possibly remove procfs support
*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mm.h>
#ifdef SIOCETHTOOL
#include <linux/ethtool.h>
#endif
#include <linux/proc_fs.h>
#include <asm/semaphore.h>
#include <asm/hvcall.h>
#include <asm/atomic.h>
#include <asm/pci_dma.h>
#include <asm/vio.h>
#include <asm/uaccess.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

#include "ibmveth.h"

#define DEBUG 1

#define ibmveth_printk(fmt, args...) \
  printk(KERN_INFO "%s: " fmt, __FILE__, ## args)

#define ibmveth_error_printk(fmt, args...) \
  printk(KERN_ERR "(%s:%3.3d ua:%lx) ERROR: " fmt, __FILE__, __LINE__ , adapter->vdev->unit_address, ## args)

#ifdef DEBUG
#define ibmveth_debug_printk_no_adapter(fmt, args...) \
  printk(KERN_DEBUG "(%s:%3.3d): " fmt, __FILE__, __LINE__ , ## args)
#define ibmveth_debug_printk(fmt, args...) \
  printk(KERN_DEBUG "(%s:%3.3d ua:%lx): " fmt, __FILE__, __LINE__ , adapter->vdev->unit_address, ## args)
#define ibmveth_assert(expr) \
  if(!(expr)) {                                   \
    printk(KERN_DEBUG "assertion failed (%s:%3.3d ua:%lx): %s\n", __FILE__, __LINE__, adapter->vdev->unit_address, #expr); \
    BUG(); \
  }
#else
#define ibmveth_debug_printk_no_adapter(fmt, args...)
#define ibmveth_debug_printk(fmt, args...)
#define ibmveth_assert(expr) 
#endif

static int ibmveth_open(struct net_device *dev);
static int ibmveth_close(struct net_device *dev);
static int ibmveth_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
static int ibmveth_poll(struct net_device *dev, int *budget);
static int ibmveth_start_xmit(struct sk_buff *skb, struct net_device *dev);
static struct net_device_stats *ibmveth_get_stats(struct net_device *dev);
static void ibmveth_set_multicast_list(struct net_device *dev);
static void ibmveth_proc_register_driver(void);
static void ibmveth_proc_unregister_driver(void);
static void ibmveth_proc_register_adapter(struct ibmveth_adapter *adapter);
static void ibmveth_proc_unregister_adapter(struct ibmveth_adapter *adapter);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
static irqreturn_t ibmveth_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
#define INIT_BOTTOM_HALF(x,y,z) INIT_WORK(x, y, (void*)z)
#define SCHEDULE_BOTTOM_HALF(x) schedule_work(x)
#else
static void ibmveth_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
#define INIT_BOTTOM_HALF(x,y,z) tasklet_init(x, y, (unsigned long)z)
#define SCHEDULE_BOTTOM_HALF(x) tasklet_schedule(x)
#endif

#ifdef CONFIG_PROC_FS
#define IBMVETH_PROC_DIR "ibmveth"
static struct proc_dir_entry *ibmveth_proc_dir;
#endif

char ibmveth_driver_name[] = "ibmveth";
char ibmveth_driver_string[] = "IBM i/pSeries Virtual Ethernet Driver";
char ibmveth_driver_version[] = "1.0";

char ibmveth_driver_cvs_version[] = "$Revision: 1.7 $";
char ibmveth_driver_cvs_tag[] = "$Name:  $";

MODULE_AUTHOR("Dave Larson <larson1@us.ibm.com>");
MODULE_DESCRIPTION("IBM i/pSeries Virtual Ethernet Driver");
MODULE_LICENSE("GPL");

/* simple methods of getting data from the current rxq entry */
static inline int ibmveth_rxq_pending_buffer(struct ibmveth_adapter *adapter)
{
    return (adapter->rx_queue.queue_addr[adapter->rx_queue.index].toggle == adapter->rx_queue.toggle);
}

static inline int ibmveth_rxq_buffer_valid(struct ibmveth_adapter *adapter)
{
    return (adapter->rx_queue.queue_addr[adapter->rx_queue.index].valid);
}

static inline int ibmveth_rxq_frame_offset(struct ibmveth_adapter *adapter)
{
    return (adapter->rx_queue.queue_addr[adapter->rx_queue.index].offset);
}

static inline int ibmveth_rxq_frame_length(struct ibmveth_adapter *adapter)
{
    return (adapter->rx_queue.queue_addr[adapter->rx_queue.index].length);
}

/* setup the initial settings for a buffer pool */
static void ibmveth_init_buffer_pool(struct ibmveth_buff_pool *pool, u32 pool_index, u32 pool_size, u32 buff_size)
{
    pool->size = pool_size;
    pool->index = pool_index;
    pool->buff_size = buff_size;
    pool->threshold = pool_size / 2;
}

/* allocate and setup an buffer pool - called during open */
static int ibmveth_alloc_buffer_pool(struct ibmveth_buff_pool *pool)
{
    int i;

    pool->free_map = kmalloc(sizeof(u16) * pool->size, GFP_KERNEL); 

    if(!pool->free_map) {
	/* ibmveth_cleanup will take care of cleanup in this case */
	return -1;
    }

    pool->dma_addr = kmalloc(sizeof(dma_addr_t) * pool->size, GFP_KERNEL); 
    pool->skbuff = kmalloc(sizeof(void*) * pool->size, GFP_KERNEL);

    if(!pool->skbuff || !pool->dma_addr) {
	/* ibmveth_cleanup will take care of cleanup in this case */
	return -1;
    }

    memset(pool->skbuff, 0, sizeof(void*) * pool->size);
    memset(pool->dma_addr, 0, sizeof(dma_addr_t) * pool->size);

    for(i = 0; i < pool->size; ++i) {
	pool->free_map[i] = i;
    }

    atomic_set(&pool->available, 0);
    pool->producer_index = 0;
    pool->consumer_index = 0;

    return 0;
}

/* replenish the buffers for a pool */
static void ibmveth_replenish_buffer_pool(struct ibmveth_adapter *adapter, struct ibmveth_buff_pool *pool)
{
    u32 i;
    u32 count = pool->size - atomic_read(&pool->available);
    u32 buffers_added = 0;

    mb();

    for(i = 0; i < count; ++i) {
	struct sk_buff *skb;
	unsigned int free_index, index;
	u64 correlator;
	union ibmveth_buf_desc desc;
	unsigned long lpar_rc;
	dma_addr_t dma_addr;

	skb = alloc_skb(pool->buff_size, GFP_ATOMIC);

	if(!skb) {
	    ibmveth_debug_printk("replenish: unable to allocate skb\n");
	    adapter->replenish_no_mem++;
	    break;
	}

	free_index = pool->consumer_index++ % pool->size;
	index = pool->free_map[free_index];
	
	ibmveth_assert(index != 0xFFFF);
	ibmveth_assert(pool->skbuff[index] == NULL);

	dma_addr = vio_map_single(adapter->vdev, skb->data, pool->buff_size, PCI_DMA_FROMDEVICE);

	pool->dma_addr[index] = dma_addr;
	pool->skbuff[index] = skb;

	correlator = ((u64)pool->index << 32) | index;
	*(u64*)skb->data = correlator;

	desc.desc = 0;
	desc.fields.valid = 1;
	desc.fields.length = pool->buff_size;
	desc.fields.address = dma_addr; 

	lpar_rc = h_add_logical_lan_buffer(adapter->vdev->unit_address, desc.desc);
		    
	if(lpar_rc != H_Success) {
	    pool->skbuff[index] = NULL;
	    pool->consumer_index--;
	    vio_unmap_single(adapter->vdev, pool->dma_addr[index], pool->buff_size, PCI_DMA_FROMDEVICE);
	    dev_kfree_skb_any(skb);
	    adapter->replenish_add_buff_failure++;
	    break;
	} else {
	    pool->free_map[free_index] = 0xFFFF;
	    buffers_added++;
	    adapter->replenish_add_buff_success++;
	}
    }
    
    atomic_add(buffers_added, &(pool->available));
}

/* check if replenishing is needed */
static inline int ibmveth_is_replenishing_needed(struct ibmveth_adapter *adapter)
{
    return ((atomic_read(&adapter->rx_buff_pool[0].available) < adapter->rx_buff_pool[0].threshold) ||
	    (atomic_read(&adapter->rx_buff_pool[1].available) < adapter->rx_buff_pool[1].threshold) ||
	    (atomic_read(&adapter->rx_buff_pool[2].available) < adapter->rx_buff_pool[2].threshold));
}

/* replenish tasklet routine */
static void ibmveth_replenish_task(struct ibmveth_adapter *adapter) 
{
    adapter->replenish_task_cycles++;

    ibmveth_replenish_buffer_pool(adapter, &adapter->rx_buff_pool[0]);
    ibmveth_replenish_buffer_pool(adapter, &adapter->rx_buff_pool[1]);
    ibmveth_replenish_buffer_pool(adapter, &adapter->rx_buff_pool[2]);

    adapter->rx_no_buffer = *(u64*)(((char*)adapter->buffer_list_addr) + 4096 - 8);

    if(ibmveth_is_replenishing_needed(adapter) || (atomic_dec_return(&adapter->in_replenish_task) > 0)) {
	/* there is more work, or we didn't change in_replenish_task from 1 -> 0 */
	atomic_set(&adapter->in_replenish_task, 1);
	SCHEDULE_BOTTOM_HALF(&adapter->replenish_task);
    }
}

/* kick the replenish tasklet if we need replenishing and it isn't already running */
static inline void ibmveth_schedule_replenishing(struct ibmveth_adapter *adapter)
{
    if(ibmveth_is_replenishing_needed(adapter)) {	
	if(atomic_inc_return(&adapter->in_replenish_task) == 1) {
		SCHEDULE_BOTTOM_HALF(&adapter->replenish_task);
       }
    }
}

/* empty and free ana buffer pool - also used to do cleanup in error paths */
static void ibmveth_free_buffer_pool(struct ibmveth_adapter *adapter, struct ibmveth_buff_pool *pool)
{
    int i;

    if(pool->free_map) {
	kfree(pool->free_map);
	pool->free_map  = NULL;
    }

    if(pool->skbuff && pool->dma_addr) {
	for(i = 0; i < pool->size; ++i) {
	    struct sk_buff *skb = pool->skbuff[i];
	    if(skb) {
		vio_unmap_single(adapter->vdev,
				 pool->dma_addr[i],
				 pool->buff_size,
				 PCI_DMA_FROMDEVICE);
		dev_kfree_skb_any(skb);
		 pool->skbuff[i] = NULL;
	    }
	}
    }

    if(pool->dma_addr) {
	kfree(pool->dma_addr);
	pool->dma_addr = NULL;
    }

    if(pool->skbuff) {
	kfree(pool->skbuff);
	pool->skbuff = NULL;
    }
}

/* remove a buffer from a pool */
static void ibmveth_remove_buffer_from_pool(struct ibmveth_adapter *adapter, u64 correlator)
{
    unsigned int pool  = correlator >> 32;
    unsigned int index = correlator & 0xFFFFFFFF;
    unsigned int free_index;
    struct sk_buff *skb;

    ibmveth_assert(pool < IbmVethNumBufferPools);
    ibmveth_assert(index < adapter->rx_buff_pool[pool].size);

    skb = adapter->rx_buff_pool[pool].skbuff[index];

    ibmveth_assert(skb != NULL);

    adapter->rx_buff_pool[pool].skbuff[index] = NULL;

    vio_unmap_single(adapter->vdev,
		     adapter->rx_buff_pool[pool].dma_addr[index],
		     adapter->rx_buff_pool[pool].buff_size,
		     PCI_DMA_FROMDEVICE);

    free_index = adapter->rx_buff_pool[pool].producer_index++ % adapter->rx_buff_pool[pool].size;
    adapter->rx_buff_pool[pool].free_map[free_index] = index;

    mb();

    atomic_dec(&(adapter->rx_buff_pool[pool].available));
}

/* get the current buffer on the rx queue */
static inline struct sk_buff *ibmveth_rxq_get_buffer(struct ibmveth_adapter *adapter)
{
    u64 correlator = adapter->rx_queue.queue_addr[adapter->rx_queue.index].correlator;
    unsigned int pool = correlator >> 32;
    unsigned int index = correlator & 0xFFFFFFFF;

    ibmveth_assert(pool < IbmVethNumBufferPools);
    ibmveth_assert(index < adapter->rx_buff_pool[pool].size);

    return adapter->rx_buff_pool[pool].skbuff[index];
}

/* recycle the current buffer on the rx queue */
static void ibmveth_rxq_recycle_buffer(struct ibmveth_adapter *adapter)
{
    u32 q_index = adapter->rx_queue.index;
    u64 correlator = adapter->rx_queue.queue_addr[q_index].correlator;
    unsigned int pool = correlator >> 32;
    unsigned int index = correlator & 0xFFFFFFFF;
    union ibmveth_buf_desc desc;
    unsigned long lpar_rc;

    ibmveth_assert(pool < IbmVethNumBufferPools);
    ibmveth_assert(index < adapter->rx_buff_pool[pool].size);

    desc.desc = 0;
    desc.fields.valid = 1;
    desc.fields.length = adapter->rx_buff_pool[pool].buff_size;
    desc.fields.address = adapter->rx_buff_pool[pool].dma_addr[index];

    lpar_rc = h_add_logical_lan_buffer(adapter->vdev->unit_address, desc.desc);
		    
    if(lpar_rc != H_Success) {
	ibmveth_debug_printk("h_add_logical_lan_buffer failed during recycle rc=%ld", lpar_rc);
	ibmveth_remove_buffer_from_pool(adapter, adapter->rx_queue.queue_addr[adapter->rx_queue.index].correlator);
    }

    if(++adapter->rx_queue.index == adapter->rx_queue.num_slots) {
	adapter->rx_queue.index = 0;
	adapter->rx_queue.toggle = !adapter->rx_queue.toggle;
    }
}

static inline void ibmveth_rxq_harvest_buffer(struct ibmveth_adapter *adapter)
{
    ibmveth_remove_buffer_from_pool(adapter, adapter->rx_queue.queue_addr[adapter->rx_queue.index].correlator);

    if(++adapter->rx_queue.index == adapter->rx_queue.num_slots) {
	adapter->rx_queue.index = 0;
	adapter->rx_queue.toggle = !adapter->rx_queue.toggle;
    }
}

static void ibmveth_cleanup(struct ibmveth_adapter *adapter)
{
    if(adapter->buffer_list_addr != NULL) {
	if(adapter->buffer_list_dma != NO_TCE) {
	    vio_unmap_single(adapter->vdev, adapter->buffer_list_dma, 4096, PCI_DMA_BIDIRECTIONAL);
	    adapter->buffer_list_dma = NO_TCE;
	}
	free_page((unsigned long)adapter->buffer_list_addr);
	adapter->buffer_list_addr = NULL;
    } 

    if(adapter->filter_list_addr != NULL) {
	if(adapter->filter_list_dma != NO_TCE) {
	    vio_unmap_single(adapter->vdev, adapter->filter_list_dma, 4096, PCI_DMA_BIDIRECTIONAL);
	    adapter->filter_list_dma = NO_TCE;
	}
	free_page((unsigned long)adapter->filter_list_addr);
	adapter->filter_list_addr = NULL;
    }

    if(adapter->rx_queue.queue_addr != NULL) {
	if(adapter->rx_queue.queue_dma != NO_TCE) {
	    vio_unmap_single(adapter->vdev, adapter->rx_queue.queue_dma, adapter->rx_queue.queue_len, PCI_DMA_BIDIRECTIONAL);
	    adapter->rx_queue.queue_dma = NO_TCE;
	}
	kfree(adapter->rx_queue.queue_addr);
	adapter->rx_queue.queue_addr = NULL;
    }

    ibmveth_free_buffer_pool(adapter, &adapter->rx_buff_pool[0]);
    ibmveth_free_buffer_pool(adapter, &adapter->rx_buff_pool[1]);
    ibmveth_free_buffer_pool(adapter, &adapter->rx_buff_pool[2]);
}

static int ibmveth_open(struct net_device *netdev)
{
    struct ibmveth_adapter *adapter = netdev->priv;
    u64 mac_address = 0;
    int rxq_entries;
    unsigned long lpar_rc;
    union ibmveth_buf_desc rxq_desc;

    ibmveth_debug_printk("open starting\n");

    rxq_entries =
      adapter->rx_buff_pool[0].size +
      adapter->rx_buff_pool[1].size +
      adapter->rx_buff_pool[2].size + 1;
    
    adapter->buffer_list_addr = (void*) get_zeroed_page(GFP_KERNEL);
    adapter->filter_list_addr = (void*) get_zeroed_page(GFP_KERNEL);
 
    if(!adapter->buffer_list_addr || !adapter->filter_list_addr) {
	ibmveth_error_printk("unable to allocate filter or buffer list pages\n");
	ibmveth_cleanup(adapter);
	return -ENOMEM;
    }

    adapter->rx_queue.queue_len = sizeof(struct ibmveth_rx_q_entry) * rxq_entries;
    adapter->rx_queue.queue_addr = kmalloc(adapter->rx_queue.queue_len, GFP_KERNEL);

    if(!adapter->rx_queue.queue_addr) {
	ibmveth_error_printk("unable to allocate rx queue pages\n");
	ibmveth_cleanup(adapter);
	return -ENOMEM;
    }

    adapter->buffer_list_dma = vio_map_single(adapter->vdev, adapter->buffer_list_addr, 4096, PCI_DMA_BIDIRECTIONAL);
    adapter->filter_list_dma = vio_map_single(adapter->vdev, adapter->filter_list_addr, 4096, PCI_DMA_BIDIRECTIONAL);
    adapter->rx_queue.queue_dma = vio_map_single(adapter->vdev, adapter->rx_queue.queue_addr, adapter->rx_queue.queue_len, PCI_DMA_BIDIRECTIONAL);

    if((adapter->buffer_list_dma == NO_TCE) || 
       (adapter->filter_list_dma == NO_TCE) || 
       (adapter->rx_queue.queue_dma == NO_TCE)) {
	ibmveth_error_printk("unable to map filter or buffer list pages\n");
	ibmveth_cleanup(adapter);
	return -ENOMEM;
    }

    adapter->rx_queue.index = 0;
    adapter->rx_queue.num_slots = rxq_entries;
    adapter->rx_queue.toggle = 1;

    if(ibmveth_alloc_buffer_pool(&adapter->rx_buff_pool[0]) ||
       ibmveth_alloc_buffer_pool(&adapter->rx_buff_pool[1]) ||
       ibmveth_alloc_buffer_pool(&adapter->rx_buff_pool[2]))
    {
	ibmveth_error_printk("unable to allocate buffer pools\n");
	ibmveth_cleanup(adapter);
	return -ENOMEM;
    }

    atomic_set(&adapter->in_replenish_task, 0);    

    memcpy(&mac_address, netdev->dev_addr, netdev->addr_len);
    mac_address = mac_address >> 16;

    rxq_desc.desc = 0;
    rxq_desc.fields.valid = 1;
    rxq_desc.fields.length = adapter->rx_queue.queue_len;
    rxq_desc.fields.address = adapter->rx_queue.queue_dma;

    ibmveth_debug_printk("buffer list @ 0x%p\n", adapter->buffer_list_addr);
    ibmveth_debug_printk("filter list @ 0x%p\n", adapter->filter_list_addr);
    ibmveth_debug_printk("receive q   @ 0x%p\n", adapter->rx_queue.queue_addr);

    
    lpar_rc = h_register_logical_lan(adapter->vdev->unit_address,
				     adapter->buffer_list_dma,
				     rxq_desc.desc,
				     adapter->filter_list_dma,
				     mac_address);

    if(lpar_rc != H_Success) {
	ibmveth_error_printk("h_register_logical_lan failed with %ld\n", lpar_rc);
	ibmveth_error_printk("buffer TCE:0x%x filter TCE:0x%x rxq desc:0x%lx MAC:0x%lx\n",
			     adapter->buffer_list_dma,
			     adapter->filter_list_dma,
			     rxq_desc.desc,
			     mac_address);
	ibmveth_cleanup(adapter);
	return -ENONET; /* TODO: what error should we use? */
    }

    ibmveth_debug_printk("registering irq 0x%x\n", netdev->irq);
    if(request_irq(netdev->irq, &ibmveth_interrupt, 0, netdev->name, netdev) != 0) {
	ibmveth_error_printk("unable to request irq 0x%x\n", netdev->irq);
	h_free_logical_lan(adapter->vdev->unit_address);
	ibmveth_cleanup(adapter);
	return -ENONET; /* TODO: what errno should this be? */
    }

    ibmveth_debug_printk("scheduling initial replenish cycle\n");
    ibmveth_schedule_replenishing(adapter);

    ibmveth_debug_printk("open complete\n");

    return 0;
}

static int ibmveth_close(struct net_device *netdev)
{
    struct ibmveth_adapter *adapter = netdev->priv;
    long lpar_rc;
    
    ibmveth_debug_printk("close starting\n");

    free_irq(netdev->irq, netdev);

    while(atomic_read(&adapter->in_replenish_task)) {
	udelay(100);
    }

    lpar_rc = h_free_logical_lan(adapter->vdev->unit_address);

    if(lpar_rc != H_Success)
    {
	ibmveth_error_printk("h_free_logical_lan failed with %lx, continuing with close\n",
			     lpar_rc);
    }

    adapter->rx_no_buffer = *(u64*)(((char*)adapter->buffer_list_addr) + 4096 - 8);

    ibmveth_cleanup(adapter);

    ibmveth_debug_printk("close complete\n");

    return 0;
}

static int ibmveth_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
#ifdef SIOCETHTOOL
    struct ibmveth_adapter *adapter = dev->priv;
    u32 ethtool_command; 

    if (cmd != SIOCETHTOOL) {
	ibmveth_debug_printk("unsupported ioctl 0x%xd\n", cmd);
 	return -EOPNOTSUPP;
    }

    if(get_user(ethtool_command, (u32*)ifr->ifr_data)) {
	ibmveth_debug_printk("unable to copy from user memory\n");
	return -EFAULT;
    }

    ibmveth_debug_printk("SIOCETHTOOL cmd %d\n", ethtool_command); 
    switch (ethtool_command) {
	case ETHTOOL_GSET: {
		struct ethtool_cmd ecmd = {ETHTOOL_GSET};
		ecmd.supported = (SUPPORTED_1000baseT_Full | SUPPORTED_Autoneg | SUPPORTED_FIBRE);
		ecmd.advertising = (ADVERTISED_1000baseT_Full | ADVERTISED_Autoneg | ADVERTISED_FIBRE);
		ecmd.speed = SPEED_1000;
		ecmd.duplex = DUPLEX_FULL;
		ecmd.port = PORT_FIBRE;
		ecmd.phy_address = 0;
		ecmd.transceiver = XCVR_INTERNAL;
		ecmd.autoneg = AUTONEG_ENABLE;
		ecmd.maxtxpkt = 0;
		ecmd.maxrxpkt = 1;
		if(copy_to_user(ifr->ifr_data, &ecmd, sizeof(ecmd)))
		    return -EFAULT;
		return 0;
	    }	

	case ETHTOOL_GDRVINFO: {
		struct ethtool_drvinfo info = {ETHTOOL_GDRVINFO};
		strncpy(info.driver, ibmveth_driver_name, sizeof(info.driver) - 1);
		strncpy(info.version, ibmveth_driver_version, sizeof(info.version) - 1);

		if (copy_to_user(ifr->ifr_data, &info, sizeof(info)))
		    return -EFAULT;
		return 0;
	    }

	case ETHTOOL_GLINK: {
		struct ethtool_value edata = {ETHTOOL_GLINK};

		if (copy_to_user(ifr->ifr_data, &edata, sizeof(edata)))
		    return -EFAULT;
		return 0;
	    }

	default:
	    break;
    }
#endif
    return -EOPNOTSUPP;
}

#define page_offset(v) ((unsigned long)(v) & ((1 << 12) - 1))

static int ibmveth_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
    struct ibmveth_adapter *adapter = netdev->priv;
    union ibmveth_buf_desc desc[IbmVethMaxSendFrags];
    unsigned long lpar_rc;
    int nfrags = 0, curfrag;

    if ((skb_shinfo(skb)->nr_frags + 1) > IbmVethMaxSendFrags) {
	adapter->tx_linearized++;
	if(skb_linearize(skb, GFP_ATOMIC) != 0)
	{
	    ibmveth_debug_printk("tx: unable to linearize skb\n");
	    adapter->tx_linearize_failed++;
	    adapter->stats.tx_dropped++;
	    dev_kfree_skb(skb);
	    return 0;
	}
    }

    memset(&desc, 0, sizeof(desc));

    /* nfrags = number of frags after the initial fragment */
    nfrags = skb_shinfo(skb)->nr_frags;

    if(nfrags)
	adapter->tx_multidesc_send++;

    /* map the initial fragment */
    desc[0].fields.length  = nfrags ? skb->len - skb->data_len : skb->len;
    desc[0].fields.address = vio_map_single(adapter->vdev, skb->data, desc[0].fields.length, PCI_DMA_TODEVICE);
    desc[0].fields.valid   = 1;

    if(desc[0].fields.address == NO_TCE) {
	ibmveth_error_printk("tx: unable to map initial fragment\n");
	adapter->tx_map_failed++;
	adapter->stats.tx_dropped++;
	return 0;
    }

    curfrag = nfrags;

    /* map fragments past the initial portion if there are any */
    while(curfrag--) {
	skb_frag_t *frag = &skb_shinfo(skb)->frags[curfrag];
	desc[curfrag+1].fields.address = vio_map_single(adapter->vdev,
							page_address(frag->page) + frag->page_offset,
							frag->size, PCI_DMA_TODEVICE);
	desc[curfrag+1].fields.length = frag->size;
	desc[curfrag+1].fields.valid  = 1;

	if(desc[curfrag+1].fields.address == NO_TCE) {
	    ibmveth_error_printk("tx: unable to map fragment %d\n", curfrag);
	    adapter->tx_map_failed++;
	    adapter->stats.tx_dropped++;
	    while(curfrag) {
		vio_unmap_single(adapter->vdev,
				 desc[curfrag].fields.address,
				 desc[curfrag].fields.length,
				 PCI_DMA_TODEVICE);
		curfrag--;
	    }
	    return 0;
	}
    }

    /* send the frame */
    unsigned long correlator = 0;
    do {
	lpar_rc = h_send_logical_lan(adapter->vdev->unit_address,
				     desc[0].desc,
				     desc[1].desc,
				     desc[2].desc,
				     desc[3].desc,
				     desc[4].desc,
				     desc[5].desc,
				     correlator);
     } while(lpar_rc == H_Busy);

    
    if(lpar_rc != H_Success && lpar_rc != H_Dropped) {
	int i;
	ibmveth_error_printk("tx: h_send_logical_lan failed with rc=%ld\n", lpar_rc);
	for(i = 0; i < 6; i++) {
	    ibmveth_error_printk("tx: desc[%i] valid=%d, len=%d, address=0x%d\n", i,
				 desc[i].fields.valid, desc[i].fields.length, desc[i].fields.address);
	}
	adapter->tx_send_failed++;
	adapter->stats.tx_dropped++;
    } else {
	adapter->stats.tx_packets++;
	adapter->stats.tx_bytes += skb->len;
    }

    do {
	vio_unmap_single(adapter->vdev, desc[nfrags].fields.address, desc[nfrags].fields.length, PCI_DMA_TODEVICE);
    } while(--nfrags >= 0);

    dev_kfree_skb(skb);
    return 0;
}

static int ibmveth_poll(struct net_device *netdev, int *budget)
{
    struct ibmveth_adapter *adapter = netdev->priv;
    int max_frames_to_process = netdev->quota;
    int frames_processed = 0;
    int more_work = 1;
    unsigned long lpar_rc;

restart_poll:
    do {
	struct net_device *netdev = adapter->netdev;

	if(ibmveth_rxq_pending_buffer(adapter)) {
	    struct sk_buff *skb;

	    if(!ibmveth_rxq_buffer_valid(adapter)) {
		wmb();
		adapter->rx_invalid_buffer++;
		ibmveth_debug_printk("recycling invalid buffer\n");
		ibmveth_rxq_recycle_buffer(adapter);
	    } else {
		int length = ibmveth_rxq_frame_length(adapter);
		int offset = ibmveth_rxq_frame_offset(adapter);
		skb = ibmveth_rxq_get_buffer(adapter);

		ibmveth_rxq_harvest_buffer(adapter);

		skb_reserve(skb, offset);
		skb_put(skb, length);
		skb->dev = netdev;
		skb->protocol = eth_type_trans(skb, netdev);

		netif_receive_skb(skb);	/* send it up */

		adapter->stats.rx_packets++;
		adapter->stats.rx_bytes += length;
		frames_processed++;
	    }
	} else {
	    more_work = 0;
	}
    } while(more_work && (frames_processed < max_frames_to_process));

    ibmveth_schedule_replenishing(adapter);

    if(more_work) {
	/* more work to do - return that we are not done yet */
	netdev->quota -= frames_processed;
	*budget -= frames_processed;
	return 1; 
    }

    /* we think we are done - reenable interrupts, then check once more to make sure we are done */
    lpar_rc = h_vio_signal(adapter->vdev->unit_address, VIO_IRQ_ENABLE);

    ibmveth_assert(lpar_rc == H_Success);
    
    netif_rx_complete(netdev);

    if(ibmveth_rxq_pending_buffer(adapter) && netif_rx_reschedule(netdev, frames_processed))
    {
	lpar_rc = h_vio_signal(adapter->vdev->unit_address, VIO_IRQ_DISABLE);
	ibmveth_assert(lpar_rc == H_Success);
	more_work = 1;
	goto restart_poll;
    }

    /* we really are done */
    return 0;
}

static irqreturn_t ibmveth_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{   
    struct net_device *netdev = dev_instance;
    struct ibmveth_adapter *adapter = netdev->priv;
    unsigned long lpar_rc;

    if(netif_rx_schedule_prep(netdev)) {
	lpar_rc = h_vio_signal(adapter->vdev->unit_address, VIO_IRQ_DISABLE);
	ibmveth_assert(lpar_rc == H_Success);
	__netif_rx_schedule(netdev);
    }
	return IRQ_HANDLED;
}

static struct net_device_stats *ibmveth_get_stats(struct net_device *dev)
{
    struct ibmveth_adapter *adapter = dev->priv;
    return &adapter->stats;
}

static void ibmveth_set_multicast_list(struct net_device *netdev)
{
    struct ibmveth_adapter *adapter = netdev->priv;
    unsigned long lpar_rc;

    if((netdev->flags & IFF_PROMISC) || (netdev->mc_count > adapter->mcastFilterSize)) {
	lpar_rc = h_multicast_ctrl(adapter->vdev->unit_address,
				   IbmVethMcastEnableRecv |
				   IbmVethMcastDisableFiltering,
				   0);
	if(lpar_rc != H_Success) {
	    ibmveth_error_printk("h_multicast_ctrl rc=%ld when entering promisc mode\n", lpar_rc);
	}
    } else {
	struct dev_mc_list *mclist = netdev->mc_list;
	int i;
	/* clear the filter table & disable filtering */
	lpar_rc = h_multicast_ctrl(adapter->vdev->unit_address,
				   IbmVethMcastEnableRecv |
				   IbmVethMcastDisableFiltering |
				   IbmVethMcastClearFilterTable,
				   0);
	if(lpar_rc != H_Success) {
	    ibmveth_error_printk("h_multicast_ctrl rc=%ld when attempting to clear filter table\n", lpar_rc);
	}
	/* add the addresses to the filter table */
	for(i = 0; i < netdev->mc_count; ++i, mclist = mclist->next) {
	    // add the multicast address to the filter table
	      unsigned long mcast_addr = 0;
	      memcpy(((char *)&mcast_addr)+2, mclist->dmi_addr, 6);
	      lpar_rc = h_multicast_ctrl(adapter->vdev->unit_address,
					 IbmVethMcastAddFilter,
					 mcast_addr);
	    if(lpar_rc != H_Success) {
		ibmveth_error_printk("h_multicast_ctrl rc=%ld when adding an entry to the filter table\n", lpar_rc);
	    }
	}
	
	/* re-enable filtering */
	lpar_rc = h_multicast_ctrl(adapter->vdev->unit_address,
				   IbmVethMcastEnableFiltering,
				   0);
	if(lpar_rc != H_Success) {
	    ibmveth_error_printk("h_multicast_ctrl rc=%ld when enabling filtering\n", lpar_rc);
	}
    }
}

static int ibmveth_set_mac_address(struct net_device *netdev, void *p)
{
    struct ibmveth_adapter *adapter = netdev->priv;
    struct sockaddr *addr = p;
    unsigned long mac_address;
    unsigned long lpar_rc;

    if(!is_valid_ether_addr(addr->sa_data))
	return -EADDRNOTAVAIL;

    memcpy(&mac_address, addr->sa_data, netdev->addr_len);
    mac_address = mac_address >> 16;

    lpar_rc = h_change_logical_lan_mac(adapter->vdev->unit_address, mac_address);

    if(lpar_rc != H_Success) {
	ibmveth_error_printk("h_change_logical_lan_mac rc=%ld when changing mac\n", lpar_rc);
	return -1;
    }

    memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);

    return 0;
}

static int __devinit ibmveth_probe(struct vio_dev *dev, const struct vio_device_id *id)
{
	int rc;
	struct net_device *netdev;
	struct ibmveth_adapter *adapter;

	unsigned int *mac_addr_p;
	unsigned int *mcastFilterSize_p;


	ibmveth_debug_printk_no_adapter("entering ibmveth_probe for UA 0x%lx\n", 
				    dev->unit_address);

	mac_addr_p = (unsigned int *) vio_get_attribute(dev, VETH_MAC_ADDR, 0);
	if(!mac_addr_p) {
		ibmveth_printk(KERN_WARNING "Can't find VETH_MAC_ADDR attribute\n");
		return 0;
	}
	
	mcastFilterSize_p= (unsigned int *) vio_get_attribute(dev, VETH_MCAST_FILTER_SIZE, 0);
	if(!mcastFilterSize_p) {
		ibmveth_printk(KERN_WARNING "Can't find VETH_MCAST_FILTER_SIZE attribute\n");
		return 0;
	}
	
	netdev = alloc_etherdev(sizeof(struct ibmveth_adapter));

	if(!netdev)
		return -ENOMEM;

	SET_MODULE_OWNER(netdev);

	adapter = netdev->priv;
	memset(adapter, 0, sizeof(adapter));
	dev->driver_data = netdev;

	adapter->vdev = dev;
	adapter->netdev = netdev;
	adapter->mcastFilterSize= *mcastFilterSize_p;
	
	/* 	Some older boxes running PHYP non-natively have an OF that
		returns a 8-byte local-mac-address field (and the first 
		2 bytes have to be ignored) while newer boxes' OF return
		a 6-byte field. Note that IEEE 1275 specifies that 
		local-mac-address must be a 6-byte field.
		The RPA doc specifies that the first byte must be 10b, so 
		we'll just look for it to solve this 8 vs. 6 byte field issue */

	while (*((char*)mac_addr_p) != (char)(0x02))
		((char*)mac_addr_p)++;

	adapter->mac_addr = 0;
	memcpy(&adapter->mac_addr, mac_addr_p, 6);

	adapter->liobn = dev->tce_table->index;
	
	netdev->irq = dev->irq;
	netdev->mtu = 9000;
	netdev->open               = ibmveth_open;
	netdev->poll               = ibmveth_poll;
	netdev->weight             = 16;
	netdev->stop               = ibmveth_close;
	netdev->hard_start_xmit    = ibmveth_start_xmit;
	netdev->get_stats          = ibmveth_get_stats;
	netdev->set_multicast_list = ibmveth_set_multicast_list;
	netdev->set_mac_address    = ibmveth_set_mac_address;
	netdev->do_ioctl           = ibmveth_ioctl;

	memcpy(&netdev->dev_addr, &adapter->mac_addr, netdev->addr_len);

	ibmveth_init_buffer_pool(&adapter->rx_buff_pool[0], 0, IbmVethPool0DftCnt, IbmVethPool0DftSize);
	ibmveth_init_buffer_pool(&adapter->rx_buff_pool[1], 1, IbmVethPool1DftCnt, IbmVethPool1DftSize);
	ibmveth_init_buffer_pool(&adapter->rx_buff_pool[2], 2, IbmVethPool2DftCnt, IbmVethPool2DftSize);

	ibmveth_debug_printk("adapter @ 0x%p\n", adapter);

	INIT_BOTTOM_HALF(&adapter->replenish_task, (void*)ibmveth_replenish_task, adapter);

	adapter->buffer_list_dma = NO_TCE;
	adapter->filter_list_dma = NO_TCE;
	adapter->rx_queue.queue_dma = NO_TCE;

	ibmveth_debug_printk("registering netdev...\n");

	rc = register_netdev(netdev);

	if(rc) {
		ibmveth_debug_printk("failed to register netdev rc=%d\n", rc);
		kfree(dev);
		return rc;
	}

	ibmveth_debug_printk("registered\n");

	ibmveth_proc_register_adapter(adapter);

	return 0;
}

static void __devexit ibmveth_remove(struct vio_dev *dev)
{
    struct net_device *netdev = dev->driver_data;
    struct ibmveth_adapter *adapter = netdev->priv;

    unregister_netdev(netdev);

    ibmveth_proc_unregister_adapter(adapter);

    kfree(netdev);
    return;
}

#ifdef CONFIG_PROC_FS
static int ibmveth_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
    struct ibmveth_adapter *adapter = data;
    char *buf = page;
    char *current_mac = ((char*) &adapter->netdev->dev_addr);
    char *firmware_mac = ((char*) &adapter->mac_addr) ;
    int len = 0;

    buf += sprintf(buf, "%s %s\n\n", ibmveth_driver_string, ibmveth_driver_version);
    buf += sprintf(buf, "Detailed Version Info: %s %s\n\n", ibmveth_driver_cvs_tag, ibmveth_driver_cvs_version);

    buf += sprintf(buf, "Unit Address:    0x%lx\n", adapter->vdev->unit_address);
    buf += sprintf(buf, "LIOBN:           0x%lx\n", adapter->liobn);
    buf += sprintf(buf, "Current MAC:     %02X:%02X:%02X:%02X:%02X:%02X\n",
		   current_mac[0], current_mac[1], current_mac[2],
		   current_mac[3], current_mac[4], current_mac[5]);
    buf += sprintf(buf, "Firmware MAC:    %02X:%02X:%02X:%02X:%02X:%02X\n",
		   firmware_mac[0], firmware_mac[1], firmware_mac[2],
		   firmware_mac[3], firmware_mac[4], firmware_mac[5]);

    buf += sprintf(buf, "\nAdapter Statistics:\n");
    buf += sprintf(buf, "  TX:  skbuffs linearized:          %ld\n", adapter->tx_linearized);
    buf += sprintf(buf, "       multi-descriptor sends:      %ld\n", adapter->tx_multidesc_send);
    buf += sprintf(buf, "       skb_linearize failures:      %ld\n", adapter->tx_linearize_failed);
    buf += sprintf(buf, "       vio_map_single failres:      %ld\n", adapter->tx_map_failed);
    buf += sprintf(buf, "       send failures:               %ld\n", adapter->tx_send_failed);
    buf += sprintf(buf, "  RX:  replenish task cycles:       %ld\n", adapter->replenish_task_cycles);
    buf += sprintf(buf, "       alloc_skb_failures:          %ld\n", adapter->replenish_no_mem);
    buf += sprintf(buf, "       add buffer failures:         %ld\n", adapter->replenish_add_buff_failure);
    buf += sprintf(buf, "       invalid buffers:             %ld\n", adapter->rx_invalid_buffer);
    buf += sprintf(buf, "       no buffers:                  %ld\n", adapter->rx_no_buffer);

    len = buf - page;    /* len = bytes in this proc file */

    len -= off;          /* take the offset off the front of the read */
    if(len < count) {    /* if they want more than we have, then this is EOF */
	*eof = 1;
	if(len <= 0) {   /* if len -= off is < 0, then they are reading past EOF, return 0 bytes */
	    return 0;
	}
    } else {
	len = count;     /* only return what they asked for */
    }
    *start = page + off; /* set the start ptr to the beginning of what they asked for */
    return len;
}
#endif

static void ibmveth_proc_register_driver(void)
{
#ifdef CONFIG_PROC_FS
    ibmveth_proc_dir = create_proc_entry(IBMVETH_PROC_DIR, S_IFDIR, proc_net);
    SET_MODULE_OWNER(ibmveth_proc_dir);
#endif
}

static void ibmveth_proc_unregister_driver(void)
{
#ifdef CONFIG_PROC_FS
    remove_proc_entry(IBMVETH_PROC_DIR, proc_net);
#endif
}

static void ibmveth_proc_register_adapter(struct ibmveth_adapter *adapter)
{
#ifdef CONFIG_PROC_FS
    struct proc_dir_entry *entry;
    entry = create_proc_entry(adapter->netdev->name, S_IFREG, ibmveth_proc_dir);
    SET_MODULE_OWNER(entry);
    entry->data = (void *) adapter;
    entry->read_proc = ibmveth_proc_read;
#endif
}

static void ibmveth_proc_unregister_adapter(struct ibmveth_adapter *adapter)
{
#ifdef CONFIG_PROC_FS
    remove_proc_entry(adapter->netdev->name, ibmveth_proc_dir);
#endif
}


static struct vio_device_id ibmveth_device_table[] __devinitdata= {
    { "network", "IBM,l-lan"},
    { 0,}
};

MODULE_DEVICE_TABLE(vio, ibmveth_device_table);

static struct vio_driver ibmveth_driver = {
    .name        = ibmveth_driver_name,
    .id_table    = ibmveth_device_table,
    .probe       = ibmveth_probe,
    .remove      = ibmveth_remove
};

static int __init ibmveth_module_init(void)
{
    int rc;

    ibmveth_printk("%s: %s %s\n", ibmveth_driver_name, ibmveth_driver_string, ibmveth_driver_version);

    ibmveth_proc_register_driver();

    rc = vio_module_init(&ibmveth_driver);

    return rc;
}

static void __exit ibmveth_module_exit(void)
{
	vio_unregister_driver(&ibmveth_driver);
	ibmveth_proc_unregister_driver();
}	

module_init(ibmveth_module_init);
module_exit(ibmveth_module_exit);
