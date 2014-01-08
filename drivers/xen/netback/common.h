/******************************************************************************
 * arch/xen/drivers/netif/backend/common.h
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

#ifndef __NETIF__BACKEND__COMMON_H__
#define __NETIF__BACKEND__COMMON_H__

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/wait.h>
#include <xen/interface/io/netif.h>
#include <xen/barrier.h>
#include <xen/xenbus.h>
#include <xen/interface/event_channel.h>

#define DPRINTK(_f, _a...)			\
	pr_debug("(file=%s, line=%d) " _f,	\
		 __FILE__ , __LINE__ , ## _a )

typedef struct netif_st {
	/* Unique identifier for this interface. */
	domid_t          domid;
	unsigned int     group;
	unsigned int     handle;

	u8               fe_dev_addr[6];

	unsigned int     irq;

	struct netif_st *notify_link[2];
#define RX_IDX 0
#define TX_IDX 1
#define rx_notify_link notify_link[RX_IDX]
#define tx_notify_link notify_link[TX_IDX]

	/* The shared rings and indexes. */
	netif_tx_back_ring_t tx;
	netif_rx_back_ring_t rx;
	struct vm_struct *tx_comms_area;
	struct vm_struct *rx_comms_area;

	/* Flags that must not be set in dev->features */
	int features_disabled;

	/* Frontend feature information. */
	u8 can_sg:1;
	u8 gso:1;
	u8 ip_csum:1;
	u8 ipv6_csum:1;

	/* Internal feature information. */
	u8 can_queue:1;	/* can queue packets for receiver? */
	u8 copying_receiver:1;	/* copy packets to receiver?       */

	u8 busted:1;

	/* Allow netif_be_start_xmit() to peek ahead in the rx request ring. */
	RING_IDX rx_req_cons_peek;

	/* Transmit shaping: allow 'credit_bytes' every 'credit_usec'. */
	unsigned long   credit_bytes;
	unsigned long   credit_usec;
	unsigned long   remaining_credit;
	struct timer_list credit_timeout;
	u64 credit_window_start;

	/* Enforce draining of the transmit queue. */
	struct timer_list tx_queue_timeout;

	/* Statistics */
	unsigned long nr_copied_skbs;
	unsigned long rx_gso_csum_fixups;

	/* Miscellaneous private stuff. */
	struct list_head list;  /* scheduling list */
	atomic_t         refcnt;
	struct net_device *dev;

	unsigned int carrier;

	wait_queue_head_t waiting_to_free;
} netif_t;

/*
 * Implement our own carrier flag: the network stack's version causes delays
 * when the carrier is re-enabled (in particular, dev_activate() may not
 * immediately be called, which can cause packet loss; also the etherbridge
 * can be rather lazy in activating its port).
 */
#define netback_carrier_on(netif)	((netif)->carrier = 1)
#define netback_carrier_off(netif)	((netif)->carrier = 0)
#define netback_carrier_ok(netif)	((netif)->carrier)

enum {
	NETBK_DONT_COPY_SKB,
	NETBK_DELAYED_COPY_SKB,
	NETBK_ALWAYS_COPY_SKB,
};

extern int netbk_copy_skb_mode;

/* Function pointers into netback accelerator plugin modules */
struct netback_accel_hooks {
	struct module *owner;
	int  (*probe)(struct xenbus_device *dev);
	int (*remove)(struct xenbus_device *dev);
};

/* Structure to track the state of a netback accelerator plugin */
struct netback_accelerator {
	struct list_head link;
	int id;
	char *eth_name;
	atomic_t use_count;
	struct netback_accel_hooks *hooks;
};

struct backend_info {
	struct xenbus_device *dev;
	netif_t *netif;
	enum xenbus_state frontend_state;
	struct xenbus_watch hotplug_status_watch;
	int have_hotplug_status_watch:1;

	/* State relating to the netback accelerator */
	void *netback_accel_priv;
	/* The accelerator that this backend is currently using */
	struct netback_accelerator *accelerator;
};

#define NETBACK_ACCEL_VERSION 0x00010001

/* 
 * Connect an accelerator plugin module to netback.  Returns zero on
 * success, < 0 on error, > 0 (with highest version number supported)
 * if version mismatch.
 */
extern int netback_connect_accelerator(unsigned version,
				       int id, const char *eth_name, 
				       struct netback_accel_hooks *hooks);
/* Disconnect a previously connected accelerator plugin module */
extern void netback_disconnect_accelerator(int id, const char *eth_name);


extern
void netback_probe_accelerators(struct backend_info *be,
				struct xenbus_device *dev);
extern
void netback_remove_accelerators(struct backend_info *be,
				 struct xenbus_device *dev);
extern
void netif_accel_init(void);


#define NET_TX_RING_SIZE __CONST_RING_SIZE(netif_tx, PAGE_SIZE)
#define NET_RX_RING_SIZE __CONST_RING_SIZE(netif_rx, PAGE_SIZE)

void netif_disconnect(struct backend_info *be);

netif_t *netif_alloc(struct device *parent, domid_t domid, unsigned int handle);
int netif_map(struct backend_info *be, grant_ref_t tx_ring_ref,
	      grant_ref_t rx_ring_ref, evtchn_port_t evtchn);

#define netif_get(_b) (atomic_inc(&(_b)->refcnt))
#define netif_put(_b)						\
	do {							\
		if ( atomic_dec_and_test(&(_b)->refcnt) )	\
			wake_up(&(_b)->waiting_to_free);	\
	} while (0)

void netif_xenbus_init(void);

#define netif_schedulable(netif)				\
	(likely(!(netif)->busted) &&				\
	 netif_running((netif)->dev) &&	netback_carrier_ok(netif))

void netif_schedule_work(netif_t *netif);
void netif_deschedule_work(netif_t *netif);

int netif_be_start_xmit(struct sk_buff *skb, struct net_device *dev);
irqreturn_t netif_be_int(int irq, void *dev_id);

static inline int netbk_can_queue(struct net_device *dev)
{
	netif_t *netif = netdev_priv(dev);
	return netif->can_queue;
}

static inline int netbk_can_sg(struct net_device *dev)
{
	netif_t *netif = netdev_priv(dev);
	return netif->can_sg;
}

struct pending_tx_info {
	netif_tx_request_t req;
	grant_handle_t grant_handle;
	netif_t *netif;
};
typedef unsigned int pending_ring_idx_t;

struct netbk_rx_meta {
	skb_frag_t frag;
	u16 id;
	u8 copy:2;
	u8 tail:1;
};

struct netbk_tx_pending_inuse {
	struct list_head list;
	unsigned long alloc_time;
};

#define MAX_PENDING_REQS (1U << CONFIG_XEN_NETDEV_TX_SHIFT)
#define MAX_MFN_ALLOC 64

struct xen_netbk {
	atomic_t nr_groups;

	struct {
		pending_ring_idx_t pending_prod, pending_cons;
		pending_ring_idx_t dealloc_prod, dealloc_cons;
		struct sk_buff_head queue;
		struct tasklet_struct tasklet;
		struct list_head schedule_list;
		spinlock_t schedule_list_lock;
		spinlock_t release_lock;
		struct page **mmap_pages;
		struct timer_list pending_timer;
		struct list_head pending_inuse_head;
		struct pending_tx_info pending_info[MAX_PENDING_REQS];
		struct netbk_tx_pending_inuse pending_inuse[MAX_PENDING_REQS];
		u16 pending_ring[MAX_PENDING_REQS];
		u16 dealloc_ring[MAX_PENDING_REQS];
		union {
			gnttab_map_grant_ref_t map_ops[MAX_PENDING_REQS];
			gnttab_unmap_grant_ref_t unmap_ops[MAX_PENDING_REQS];
			gnttab_copy_t copy_ops[2 * MAX_PENDING_REQS - 1];
			multicall_entry_t mcl[0];
		};
		gnttab_copy_t copy_op;
		netif_tx_request_t slots[XEN_NETIF_NR_SLOTS_MIN];
	} tx;

	wait_queue_head_t action_wq;
	struct task_struct *task;

	struct xen_netbk_rx {
		struct sk_buff_head queue;
		struct tasklet_struct tasklet;
		struct timer_list timer;
		unsigned int alloc_index;
		struct multicall_entry mcl[NET_RX_RING_SIZE+3];
		struct mmu_update mmu[NET_RX_RING_SIZE];
		struct gnttab_copy grant_copy_op[2 * NET_RX_RING_SIZE];
		struct gnttab_transfer grant_trans_op;
		struct netbk_rx_meta meta[NET_RX_RING_SIZE];
		unsigned long mfn_list[MAX_MFN_ALLOC];
	} rx;
};

extern struct xen_netbk *xen_netbk;
extern unsigned int netbk_nr_groups;

#endif /* __NETIF__BACKEND__COMMON_H__ */
