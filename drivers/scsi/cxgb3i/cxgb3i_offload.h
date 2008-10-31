/*
 * Copyright (C) 2003-2008 Chelsio Communications.  All rights reserved.
 *
 * Written by Dimitris Michailidis (dm@chelsio.com)
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the LICENSE file included in this
 * release for licensing terms and conditions.
 */

#ifndef _CXGB3I_OFFLOAD_H
#define _CXGB3I_OFFLOAD_H

#include <linux/skbuff.h>
#include <net/tcp.h>

#include "common.h"
#include "adapter.h"
#include "t3cdev.h"
#include "cxgb3_offload.h"

#define cxgb3i_log_error(fmt...) printk(KERN_ERR "cxgb3i: ERR! " fmt)
#define cxgb3i_log_warn(fmt...)	 printk(KERN_WARNING "cxgb3i: WARN! " fmt)
#define cxgb3i_log_info(fmt...)  printk(KERN_INFO "cxgb3i: " fmt)

#ifdef __DEBUG_CXGB3I__
#define cxgb3i_log_debug(fmt, args...) \
	printk(KERN_INFO "cxgb3i: %s - " fmt, __func__ , ## args)
#else
#define cxgb3i_log_debug(fmt...)
#endif

#ifdef __DEBUG_C3CN_CONN__
#define c3cn_conn_debug         cxgb3i_log_debug
#else
#define c3cn_conn_debug(fmt...)
#endif

/*
 * Data structure to keep track of cxgb3 connection.
 */
struct s3_conn {
	struct net_device *dev;		/* net device of with connection */
	struct t3cdev *cdev;		/* adapter t3cdev for net device */
	unsigned long flags;		/* see c3cn_flags below */
	int tid;			/* ID of TCP Control Block */
	int qset;			/* queue Set used by connection */
	int mss_idx;			/* Maximum Segment Size table index */
	struct l2t_entry *l2t;		/* ARP resolution for offload packets */
	int wr_max;			/* maximum in-flight writes */
	int wr_avail;			/* number of writes available */
	int wr_unacked;			/* writes since last request for */
					/*   completion notification */
	struct sk_buff *wr_pending_head;/* head of pending write queue */
	struct sk_buff *wr_pending_tail;/* tail of pending write queue */
	struct sk_buff *ctrl_skb_cache;	/* single entry cached skb for */
					/*   short-term control operations */
	spinlock_t lock;		/* connection status lock */
	atomic_t refcnt;		/* reference count on connection */
	volatile unsigned int state;	/* connection state */
	struct sockaddr_in saddr;	/* source IP/port address */
	struct sockaddr_in daddr;	/* destination IP/port address */
	struct dst_entry *dst_cache;	/* reference to destination route */
	unsigned char shutdown;		/* shutdown status */
	struct sk_buff_head receive_queue;/* received PDUs */
	struct sk_buff_head write_queue;/* un-pushed pending writes */

	struct timer_list retry_timer;	/* retry timer for various operations */
	int err;			/* connection error status */
	rwlock_t callback_lock;		/* lock for opaque user context */
	void *user_data;		/* opaque user context */

	u32 rcv_nxt;			/* what we want to receive next */
	u32 copied_seq;			/* head of yet unread data */
	u32 rcv_wup;			/* rcv_nxt on last window update sent */
	u32 snd_nxt;			/* next sequence we send */
	u32 snd_una;			/* first byte we want an ack for */

	u32 write_seq;			/* tail+1 of data held in send buffer */
};

/* Flags in c3cn->shutdown */
#define C3CN_RCV_SHUTDOWN	0x1
#define C3CN_SEND_SHUTDOWN	0x2
#define C3CN_SHUTDOWN_MASK	(C3CN_RCV_SHUTDOWN | C3CN_SEND_SHUTDOWN)

/*
 * connection state bitmap
 */
#define C3CN_STATE_CLOSE	0x1
#define C3CN_STATE_SYN_SENT	0x2
#define C3CN_STATE_ESTABLISHED	0x4
#define C3CN_STATE_CLOSING	0x8
#define C3CN_STATE_ABORING	0x10

#define C3CN_STATE_MASK		0xFF

static inline unsigned int c3cn_in_state(const struct s3_conn *c3cn,
					 unsigned int states)
{
	return states & c3cn->state;
}

/*
 * Connection flags -- many to track some close related events.
 */
enum c3cn_flags {
	C3CN_ABORT_RPL_RCVD,	/* received one ABORT_RPL_RSS message */
	C3CN_ABORT_REQ_RCVD,	/* received one ABORT_REQ_RSS message */
	C3CN_TX_WAIT_IDLE,	/* suspend Tx until in-flight data is ACKed */
	C3CN_ABORT_SHUTDOWN,	/* shouldn't send more abort requests */

	C3CN_ABORT_RPL_PENDING,	/* expecting an abort reply */
	C3CN_CLOSE_CON_REQUESTED,	/* we've sent a close_conn_req */
	C3CN_TX_DATA_SENT,	/* already sent a TX_DATA WR */
	C3CN_CLOSE_NEEDED,	/* need to be closed */
	C3CN_DONE,
};

/*
 * Per adapter data.  Linked off of each Ethernet device port on the adapter.
 * Also available via the t3cdev structure since we have pointers to our port
 * net_device's there ...
 */
struct cxgb3i_sdev_data {
	struct list_head list;		/* links for list of all adapters */
	struct t3cdev *cdev;		/* adapter t3cdev */
	struct cxgb3_client *client;	/* CPL client pointer */
	struct adap_ports *ports;	/* array of adapter ports */
	unsigned int rx_page_size;	/* RX page size */
	struct sk_buff_head deferq;	/* queue for processing replies from */
					/*   worker thread context */
	struct work_struct deferq_task;	/* worker thread */
};
#define NDEV2CDATA(ndev) (*(struct cxgb3i_sdev_data **)&(ndev)->ec_ptr)
#define CXGB3_SDEV_DATA(cdev) NDEV2CDATA((cdev)->lldev)

/*
 * Primary API routines.
 */
void cxgb3i_sdev_cleanup(void);
int cxgb3i_sdev_init(cxgb3_cpl_handler_func *);
void cxgb3i_sdev_add(struct t3cdev *, struct cxgb3_client *);
void cxgb3i_sdev_remove(struct t3cdev *);

struct s3_conn *cxgb3i_c3cn_create(void);
int cxgb3i_c3cn_connect(struct s3_conn *, struct sockaddr_in *);
void cxgb3i_c3cn_rx_credits(struct s3_conn *, int);
int cxgb3i_c3cn_send_pdus(struct s3_conn *, struct sk_buff *, int);
void cxgb3i_c3cn_release(struct s3_conn *);

/*
 * Definitions for sk_buff state and ULP mode management.
 */

struct cxgb3_skb_cb {
	__u8 flags;		/* see C3CB_FLAG_* below */
	__u8 ulp_mode;		/* ULP mode/submode of sk_buff */
	__u32 seq;		/* sequence number */
	__u32 ddigest;		/* ULP rx_data_ddp selected field */
	__u32 pdulen;		/* ULP rx_data_ddp selected field */
	__u8 ulp_data[16];	/* scratch area for ULP */
};

#define CXGB3_SKB_CB(skb)	((struct cxgb3_skb_cb *)&((skb)->cb[0]))

#define skb_ulp_mode(skb)	(CXGB3_SKB_CB(skb)->ulp_mode)
#define skb_ulp_ddigest(skb)	(CXGB3_SKB_CB(skb)->ddigest)
#define skb_ulp_pdulen(skb)	(CXGB3_SKB_CB(skb)->pdulen)
#define skb_ulp_data(skb)	(CXGB3_SKB_CB(skb)->ulp_data)

enum {
	C3CB_FLAG_NEED_HDR = 1 << 0,	/* packet needs a TX_DATA_WR header */
	C3CB_FLAG_NO_APPEND = 1 << 1,	/* don't grow this skb */
	C3CB_FLAG_BARRIER = 1 << 2,	/* set TX_WAIT_IDLE after sending */
	C3CB_FLAG_COMPL = 1 << 4,	/* request WR completion */
};

/*
 * Top-level CPL message processing used by most CPL messages that
 * pertain to connections.
 */
static inline void process_cpl_msg(void (*fn)(struct s3_conn *,
					      struct sk_buff *),
				   struct s3_conn *c3cn,
				   struct sk_buff *skb)
{
	spin_lock(&c3cn->lock);
	fn(c3cn, skb);
	spin_unlock(&c3cn->lock);
}

/*
 * Opaque version of structure the SGE stores at skb->head of TX_DATA packets
 * and for which we must reserve space.
 */
struct sge_opaque_hdr {
	void *dev;
	dma_addr_t addr[MAX_SKB_FRAGS + 1];
};

/* for TX: a skb must have a headroom of at least TX_HEADER_LEN bytes */
#define TX_HEADER_LEN \
		(sizeof(struct tx_data_wr) + sizeof(struct sge_opaque_hdr))

void *cxgb3i_alloc_big_mem(unsigned int);
void cxgb3i_free_big_mem(void *);

/*
 * get and set private ip for iscsi traffic
 */
#define cxgb3i_get_private_ipv4addr(ndev) \
	(((struct port_info *)(netdev_priv(ndev)))->iscsi_ipv4addr)
#define cxgb3i_set_private_ipv4addr(ndev, addr) \
	(((struct port_info *)(netdev_priv(ndev)))->iscsi_ipv4addr) = addr

/* max. connections per adapter */
#define CXGB3I_MAX_CONN		16384
#endif /* _CXGB3_OFFLOAD_H */
