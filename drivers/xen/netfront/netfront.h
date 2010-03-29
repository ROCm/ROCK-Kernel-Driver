/******************************************************************************
 * Virtual network driver for conversing with remote driver backends.
 *
 * Copyright (c) 2002-2005, K A Fraser
 * Copyright (c) 2005, XenSource Ltd
 * Copyright (C) 2007 Solarflare Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef NETFRONT_H
#define NETFRONT_H

#include <xen/interface/io/netif.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/list.h>

#define NET_TX_RING_SIZE __CONST_RING_SIZE(netif_tx, PAGE_SIZE)
#define NET_RX_RING_SIZE __CONST_RING_SIZE(netif_rx, PAGE_SIZE)

#include <xen/xenbus.h>

#ifdef HAVE_XEN_PLATFORM_COMPAT_H
#include <xen/platform-compat.h>
#endif

/* 
 * Function pointer table for hooks into a network acceleration
 * plugin.  These are called at appropriate points from the netfront
 * driver 
 */
struct netfront_accel_hooks {
	/* 
	 * new_device: Accelerator hook to ask the plugin to support a
	 * new network interface
	 */
	int (*new_device)(struct net_device *net_dev, struct xenbus_device *dev);
	/*
	 * remove: Opposite of new_device
	 */
	int (*remove)(struct xenbus_device *dev);
	/*
	 * The net_device is being polled, check the accelerated
	 * hardware for any pending packets
	 */
	int (*netdev_poll)(struct net_device *dev, int *pbudget);
	/*
	 * start_xmit: Used to give the accelerated plugin the option
	 * of sending a packet.  Returns non-zero if has done so, or
	 * zero to decline and force the packet onto normal send
	 * path
	 */
	int (*start_xmit)(struct sk_buff *skb, struct net_device *dev);
	/* 
	 * start/stop_napi_interrupts Used by netfront to indicate
	 * when napi interrupts should be enabled or disabled 
	 */
	int (*start_napi_irq)(struct net_device *dev);
	void (*stop_napi_irq)(struct net_device *dev);
	/* 
	 * Called before re-enabling the TX queue to check the fast
	 * path has slots too
	 */
	int (*check_ready)(struct net_device *dev);
	/*
	 * Get the fastpath network statistics
	 */
	int (*get_stats)(struct net_device *dev,
			 struct net_device_stats *stats);
};


/* Version of API/protocol for communication between netfront and
   acceleration plugin supported */
#define NETFRONT_ACCEL_VERSION 0x00010003

/* 
 * Per-netfront device state for the accelerator.  This is used to
 * allow efficient per-netfront device access to the accelerator
 * hooks 
 */
struct netfront_accel_vif_state {
	struct list_head link;

	struct xenbus_device *dev;
	struct netfront_info *np;
	struct netfront_accel_hooks *hooks;

	/* Watch on the accelerator configuration value */
	struct xenbus_watch accel_watch;
	/* Work item to process change in accelerator */
	struct work_struct accel_work;
	/* The string from xenbus last time accel_watch fired */
	char *accel_frontend;
}; 

/* 
 * Per-accelerator state stored in netfront.  These form a list that
 * is used to track which devices are accelerated by which plugins,
 * and what plugins are available/have been requested 
 */
struct netfront_accelerator {
	/* Used to make a list */
	struct list_head link;
	/* ID of the accelerator */
	int id;
	/*
	 * String describing the accelerator.  Currently this is the
	 * name of the accelerator module.  This is provided by the
	 * backend accelerator through xenstore 
	 */
	char *frontend;
	/* The hooks into the accelerator plugin module */
	struct netfront_accel_hooks *hooks;

	/* 
	 * List of per-netfront device state (struct
	 * netfront_accel_vif_state) for each netfront device that is
	 * using this accelerator
	 */
	struct list_head vif_states;
	spinlock_t vif_states_lock;
};

struct netfront_info {
	struct list_head list;
	struct net_device *netdev;

	struct net_device_stats stats;

	struct netif_tx_front_ring tx;
	struct netif_rx_front_ring rx;

	spinlock_t   tx_lock;
	spinlock_t   rx_lock;

	struct napi_struct	napi;

	unsigned int irq;
	unsigned int copying_receiver;
	unsigned int carrier;

	/* Receive-ring batched refills. */
#define RX_MIN_TARGET 8
#define RX_DFL_MIN_TARGET 64
#define RX_MAX_TARGET min_t(int, NET_RX_RING_SIZE, 256)
	unsigned rx_min_target, rx_max_target, rx_target;
	struct sk_buff_head rx_batch;

	struct timer_list rx_refill_timer;

	/*
	 * {tx,rx}_skbs store outstanding skbuffs. The first entry in tx_skbs
	 * is an index into a chain of free entries.
	 */
	struct sk_buff *tx_skbs[NET_TX_RING_SIZE+1];
	struct sk_buff *rx_skbs[NET_RX_RING_SIZE];

#define TX_MAX_TARGET min_t(int, NET_RX_RING_SIZE, 256)
	grant_ref_t gref_tx_head;
	grant_ref_t grant_tx_ref[NET_TX_RING_SIZE + 1];
	grant_ref_t gref_rx_head;
	grant_ref_t grant_rx_ref[NET_RX_RING_SIZE];

	struct xenbus_device *xbdev;
	int tx_ring_ref;
	int rx_ring_ref;
	u8 mac[ETH_ALEN];

	unsigned long rx_pfn_array[NET_RX_RING_SIZE];
	struct multicall_entry rx_mcl[NET_RX_RING_SIZE+1];
	struct mmu_update rx_mmu[NET_RX_RING_SIZE];

	/* Private pointer to state internal to accelerator module */
	void *accel_priv;
	/* The accelerator used by this netfront device */
	struct netfront_accelerator *accelerator;
	/* The accelerator state for this netfront device */
	struct netfront_accel_vif_state accel_vif_state;
};


/* Exported Functions */

/*
 * Called by an accelerator plugin module when it has loaded.
 *
 * frontend: the string describing the accelerator, currently the module name 
 * hooks: the hooks for netfront to use to call into the accelerator
 * version: the version of API between frontend and plugin requested
 * 
 * return: 0 on success, <0 on error, >0 (with version supported) on
 * version mismatch
 */
extern int netfront_accelerator_loaded(int version, const char *frontend, 
				       struct netfront_accel_hooks *hooks);

/* 
 * Called by an accelerator plugin module when it is about to unload.
 *
 * frontend: the string describing the accelerator.  Must match the
 * one passed to netfront_accelerator_loaded()
 */ 
extern void netfront_accelerator_stop(const char *frontend);

/* 
 * Called by an accelerator before waking the net device's TX queue to
 * ensure the slow path has available slots.  Returns true if OK to
 * wake, false if still busy 
 */
extern int netfront_check_queue_ready(struct net_device *net_dev);


/* Internal-to-netfront Functions */

/* 
 * Call into accelerator and check to see if it has tx space before we
 * wake the net device's TX queue.  Returns true if OK to wake, false
 * if still busy
 */ 
extern 
int netfront_check_accelerator_queue_ready(struct net_device *dev,
					   struct netfront_info *np);
extern
int netfront_accelerator_call_remove(struct netfront_info *np,
				     struct xenbus_device *dev);
extern
int netfront_accelerator_suspend(struct netfront_info *np,
				 struct xenbus_device *dev);
extern
int netfront_accelerator_suspend_cancel(struct netfront_info *np,
					struct xenbus_device *dev);
extern
void netfront_accelerator_resume(struct netfront_info *np,
				 struct xenbus_device *dev);
extern
void netfront_accelerator_call_stop_napi_irq(struct netfront_info *np,
					     struct net_device *dev);
extern
int netfront_accelerator_call_get_stats(struct netfront_info *np,
					struct net_device *dev);
extern
void netfront_accelerator_add_watch(struct netfront_info *np);

extern
void netif_init_accel(void);
extern
void netif_exit_accel(void);

extern
void init_accelerator_vif(struct netfront_info *np,
			  struct xenbus_device *dev);
#endif /* NETFRONT_H */
