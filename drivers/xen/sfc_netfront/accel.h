/****************************************************************************
 * Solarflare driver for Xen network acceleration
 *
 * Copyright 2006-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications <linux-xen-drivers@solarflare.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#ifndef NETFRONT_ACCEL_H
#define NETFRONT_ACCEL_H

#include "accel_msg_iface.h"
#include "accel_cuckoo_hash.h"
#include "accel_bufs.h"

#include "etherfabric/ef_vi.h"

#include <xen/xenbus.h>
#include <xen/evtchn.h>

#include <linux/kernel.h>
#include <linux/list.h>

enum netfront_accel_post_status {
	NETFRONT_ACCEL_STATUS_GOOD,
	NETFRONT_ACCEL_STATUS_BUSY,
	NETFRONT_ACCEL_STATUS_CANT
};

#define NETFRONT_ACCEL_STATS 1
#if NETFRONT_ACCEL_STATS
#define NETFRONT_ACCEL_STATS_OP(x) x
#else
#define NETFRONT_ACCEL_STATS_OP(x)
#endif


enum netfront_accel_msg_state {
	NETFRONT_ACCEL_MSG_NONE = 0,
	NETFRONT_ACCEL_MSG_HELLO = 1,
	NETFRONT_ACCEL_MSG_HW = 2
};


typedef struct {
	u32 in_progress;
	u32 total_len;
	struct sk_buff *skb;
} netfront_accel_jumbo_state;


struct netfront_accel_ssr_state {
	/** List of tracked connections. */
	struct list_head conns;

	/** Free efx_ssr_conn instances. */
	struct list_head free_conns;
};


struct netfront_accel_netdev_stats {
	/* Fastpath stats. */
	u32 fastpath_rx_pkts;
	u32 fastpath_rx_bytes;
	u32 fastpath_rx_errors;
	u32 fastpath_tx_pkts; 
	u32 fastpath_tx_bytes;
	u32 fastpath_tx_errors;
};


struct netfront_accel_netdev_dbfs {
	struct dentry *fastpath_rx_pkts;
	struct dentry *fastpath_rx_bytes;
	struct dentry *fastpath_rx_errors;
	struct dentry *fastpath_tx_pkts; 
	struct dentry *fastpath_tx_bytes;
	struct dentry *fastpath_tx_errors;
};


struct netfront_accel_stats {
	/** Fast path events */
	u64 fastpath_tx_busy;

	/** TX DMA queue status */
	u64 fastpath_tx_completions;

	/** The number of events processed. */
	u64 event_count;

	/** Number of frame trunc events seen on fastpath */
	u64 fastpath_frm_trunc;

	/** Number of no rx descriptor trunc events seen on fastpath */
	u64 rx_no_desc_trunc;

	/** The number of misc bad events (e.g. RX_DISCARD) processed. */
	u64 bad_event_count;

	/** Number of events dealt with in poll loop */
	u32 events_per_poll_max;
	u32 events_per_poll_tx_max;
	u32 events_per_poll_rx_max;

	/** Largest number of concurrently outstanding tx descriptors */
	u32 fastpath_tx_pending_max;

	/** The number of events since the last interrupts. */
	u32 event_count_since_irq;

	/** The max number of events between interrupts. */
	u32 events_per_irq_max;

	/** The number of interrupts. */
	u64 irq_count;

	/** The number of useless interrupts. */
	u64 useless_irq_count;

	/** The number of polls scheduled. */
	u64 poll_schedule_count;

	/** The number of polls called. */
	u64 poll_call_count;

	/** The number of rechecks. */
	u64 poll_reschedule_count;

	/** Number of times we've called netif_stop_queue/netif_wake_queue */
	u64 queue_stops;
	u64 queue_wakes;

	/** SSR stats */
	u64 ssr_bursts;
	u64 ssr_drop_stream;
	u64 ssr_misorder;
	u64 ssr_slow_start;
	u64 ssr_merges;
	u64 ssr_too_many;
	u64 ssr_new_stream;
};


struct netfront_accel_dbfs {
	struct dentry *fastpath_tx_busy;
	struct dentry *fastpath_tx_completions;
	struct dentry *fastpath_tx_pending_max;
	struct dentry *fastpath_frm_trunc;
	struct dentry *rx_no_desc_trunc;
	struct dentry *event_count;
	struct dentry *bad_event_count;
	struct dentry *events_per_poll_max;
	struct dentry *events_per_poll_rx_max;
	struct dentry *events_per_poll_tx_max;
	struct dentry *event_count_since_irq;
	struct dentry *events_per_irq_max;
	struct dentry *irq_count;
	struct dentry *useless_irq_count;
	struct dentry *poll_schedule_count;
	struct dentry *poll_call_count;
	struct dentry *poll_reschedule_count;
	struct dentry *queue_stops;
	struct dentry *queue_wakes;
	struct dentry *ssr_bursts;
	struct dentry *ssr_drop_stream;
	struct dentry *ssr_misorder;
	struct dentry *ssr_slow_start;
	struct dentry *ssr_merges;
	struct dentry *ssr_too_many;
	struct dentry *ssr_new_stream;
};


typedef struct netfront_accel_vnic {
	struct netfront_accel_vnic *next;
	
	struct mutex vnic_mutex;

	spinlock_t tx_lock;

	struct netfront_accel_bufpages bufpages;
	struct netfront_accel_bufinfo *rx_bufs;
	struct netfront_accel_bufinfo *tx_bufs;
	
	/** Hardware & VI state */
	ef_vi vi;

	ef_vi_state *vi_state;

	ef_eventq_state evq_state;

	void *evq_mapping;

	/** Hardware dependant state */
	union {
		struct {
			/** Falcon A or B */
			enum net_accel_hw_type type; 
			u32 *evq_rptr;
			u32 *doorbell;
			void *evq_rptr_mapping;
			void *doorbell_mapping;
			void *txdmaq_mapping;
			void *rxdmaq_mapping;
		} falcon;
	} hw;
  
	/** RX DMA queue status */
	u32 rx_dma_level;

	/** Number of RX descriptors waiting to be pushed to the card. */
	u32 rx_dma_batched;
#define NETFRONT_ACCEL_RX_DESC_BATCH 16

	/**
	 * Hash table of remote mac addresses to decide whether to try
	 * fast path
	 */
	cuckoo_hash_table fastpath_table;
	spinlock_t table_lock;

	/** the local mac address of virtual interface we're accelerating */
	u8 mac[ETH_ALEN];

	int rx_pkt_stride;
	int rx_skb_stride;

	/**
	 * Keep track of fragments of jumbo packets as events are
	 * delivered by NIC 
	 */
	netfront_accel_jumbo_state jumbo_state;

	struct net_device *net_dev;

	/** These two gate the enabling of fast path operations */
	int frontend_ready;
	int backend_netdev_up;

	int irq_enabled;
	spinlock_t irq_enabled_lock;

	int tx_enabled;

	int poll_enabled;

	/** A spare slot for a TX packet.  This is treated as an extension
	 * of the DMA queue. */
	struct sk_buff *tx_skb;

	/** Keep track of fragments of SSR packets */
	struct netfront_accel_ssr_state ssr_state;

	struct xenbus_device *dev;

	/** Event channel for messages */
	int msg_channel;
	int msg_channel_irq;

	/** Event channel for network interrupts. */
	int net_channel;
	int net_channel_irq;

	struct net_accel_shared_page *shared_page;

	grant_ref_t ctrl_page_gnt;
	grant_ref_t msg_page_gnt;

	/** Message Qs, 1 each way. */
	sh_msg_fifo2 to_dom0;
	sh_msg_fifo2 from_dom0;

	enum netfront_accel_msg_state msg_state;

	/** Watch on accelstate */
	struct xenbus_watch backend_accel_watch;
	/** Watch on frontend's MAC address */
	struct xenbus_watch mac_address_watch;

	/** Work to process received irq/msg */
	struct work_struct msg_from_bend;

	/** Wait queue for changes in accelstate. */
	wait_queue_head_t state_wait_queue;

	/** The current accelstate of this driver. */
	XenbusState frontend_state;

	/** The most recent accelstate seen by the xenbus watch. */
	XenbusState backend_state;

	/** Non-zero if we should reject requests to connect. */
	int removing;

	/** Non-zero if the domU shared state has been initialised. */
	int domU_state_is_setup;

	/** Non-zero if the dom0 shared state has been initialised. */
	int dom0_state_is_setup;

	/* Those statistics that are added to the netdev stats */
	struct netfront_accel_netdev_stats netdev_stats;
	struct netfront_accel_netdev_stats stats_last_read;
#ifdef CONFIG_DEBUG_FS
	struct netfront_accel_netdev_dbfs netdev_dbfs;
#endif

	/* These statistics are internal and optional */
#if NETFRONT_ACCEL_STATS
	struct netfront_accel_stats stats;
#ifdef CONFIG_DEBUG_FS
	struct netfront_accel_dbfs dbfs;
#endif
#endif

	/** Debufs fs dir for this interface */
	struct dentry *dbfs_dir;
} netfront_accel_vnic;


/* Module parameters */
extern unsigned sfc_netfront_max_pages;
extern unsigned sfc_netfront_buffer_split;

extern const char *frontend_name;
extern struct netfront_accel_hooks accel_hooks;
extern struct workqueue_struct *netfront_accel_workqueue;


extern
void netfront_accel_vi_ctor(netfront_accel_vnic *vnic);

extern
int netfront_accel_vi_init(netfront_accel_vnic *vnic, 
			   struct net_accel_msg_hw *hw_msg);

extern
void netfront_accel_vi_dtor(netfront_accel_vnic *vnic);


/**
 * Add new buffers which have been registered with the NIC.
 *
 * @v   vnic     The vnic instance to process the response.
 *
 * The buffers contained in the message are added to the buffer pool.
 */
extern
void netfront_accel_vi_add_bufs(netfront_accel_vnic *vnic, int is_rx);

/**
 * Put a packet on the tx DMA queue.
 *
 * @v  vnic	 The vnic instance to accept the packet.
 * @v  skb	 A sk_buff to send.
 *
 * Attempt to send a packet.  On success, the skb is owned by the DMA
 * queue and will be released when the completion event arrives.
 */
extern enum netfront_accel_post_status
netfront_accel_vi_tx_post(netfront_accel_vnic *vnic,
			  struct sk_buff *skb);


/**
 * Process events in response to an interrupt.
 *
 * @v   vnic       The vnic instance to poll.
 * @v   rx_packets The maximum number of rx packets to process.
 * @ret rx_done    The number of rx packets processed.
 *
 * The vnic will process events until there are no more events
 * remaining or the specified number of rx packets has been processed.
 * The split from the interrupt call is to allow Linux NAPI
 * polling.
 */
extern
int netfront_accel_vi_poll(netfront_accel_vnic *vnic, int rx_packets);


/**
 * Iterate over the fragments of a packet buffer.
 *
 * @v   skb      The packet buffer to examine.
 * @v   idx      A variable name for the fragment index.
 * @v   data     A variable name for the address of the fragment data.
 * @v   length   A variable name for the fragment length.
 * @v   code     A section of code to execute for each fragment.
 *
 * This macro iterates over the fragments in a packet buffer and
 * executes the code for each of them.
 */
#define NETFRONT_ACCEL_PKTBUFF_FOR_EACH_FRAGMENT(skb, frag_idx,		\
						 frag_data, frag_len,   \
						 code)			\
	do {								\
		int frag_idx;						\
		void *frag_data;					\
		unsigned int	  frag_len;				\
									\
		frag_data = skb->data;					\
		frag_len = skb_headlen(skb);				\
		frag_idx = 0;						\
		while (1) { /* For each fragment */			\
			code;						\
			if (frag_idx >= skb_shinfo(skb)->nr_frags) {	\
				break;					\
			} else {					\
				skb_frag_t *fragment;			\
				fragment = &skb_shinfo(skb)->frags[frag_idx]; \
				frag_len = fragment->size;		\
				frag_data = ((void*)page_address(fragment->page) \
					     + fragment->page_offset);	\
			};						\
			frag_idx++;					\
		}							\
	} while(0)

static inline
void netfront_accel_disable_net_interrupts(netfront_accel_vnic *vnic)
{
	mask_evtchn(vnic->net_channel);
}

static inline
void netfront_accel_enable_net_interrupts(netfront_accel_vnic *vnic)
{
	unmask_evtchn(vnic->net_channel);
}

void netfront_accel_msg_tx_fastpath(netfront_accel_vnic *vnic, const void *mac,
				    u32 ip, u16 port, u8 protocol);

/* Process an IRQ received from back end driver */
irqreturn_t netfront_accel_msg_channel_irq_from_bend(int irq, void *context);
irqreturn_t netfront_accel_net_channel_irq_from_bend(int irq, void *context);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
extern void netfront_accel_msg_from_bend(struct work_struct *context);
#else
extern void netfront_accel_msg_from_bend(void *context);
#endif

extern void vnic_stop_fastpath(netfront_accel_vnic *vnic);

extern int netfront_accel_probe(struct net_device *net_dev, 
				struct xenbus_device *dev);
extern int netfront_accel_remove(struct xenbus_device *dev);
extern void netfront_accel_set_closing(netfront_accel_vnic *vnic);

extern int netfront_accel_vi_enable_interrupts(netfront_accel_vnic *vnic);

extern void netfront_accel_debugfs_init(void);
extern void netfront_accel_debugfs_fini(void);
extern int netfront_accel_debugfs_create(netfront_accel_vnic *vnic);
extern int netfront_accel_debugfs_remove(netfront_accel_vnic *vnic);

#endif /* NETFRONT_ACCEL_H */
