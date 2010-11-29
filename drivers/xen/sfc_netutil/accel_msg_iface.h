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

#ifndef NET_ACCEL_MSG_IFACE_H
#define NET_ACCEL_MSG_IFACE_H

#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include "accel_shared_fifo.h"

#define NET_ACCEL_MSG_MAGIC (0x85465479)

/*! We talk version 0.010 of the interdomain protocol */
#define NET_ACCEL_MSG_VERSION (0x00001000)

/*! Shared memory portion of inter-domain FIFO */
struct net_accel_msg_queue {
	u32 read;
	u32 write;
};


/*
 * The aflags in the following structure is used as follows:
 *
 *  - each bit is set when one of the corresponding variables is
 *  changed by either end.
 *
 *  - the end that has made the change then forwards an IRQ to the
 *  other
 *
 *  - the IRQ handler deals with these bits either on the fast path, or
 *  for less common changes, by jumping onto the slow path.
 *
 *  - once it has seen a change, it clears the relevant bit.
 *
 * aflags is accessed atomically using clear_bit, test_bit,
 * test_and_set_bit etc
 */

/*
 * The following used to signify to the other domain when the queue
 * they want to use is full, and when it is no longer full.  Could be
 * compressed to use fewer bits but done this way for simplicity and
 * clarity
 */

/* "dom0->domU queue" is full */
#define NET_ACCEL_MSG_AFLAGS_QUEUE0FULL      0x1 
#define NET_ACCEL_MSG_AFLAGS_QUEUE0FULL_B    0
/* "dom0->domU queue" is not full */
#define NET_ACCEL_MSG_AFLAGS_QUEUE0NOTFULL   0x2 
#define NET_ACCEL_MSG_AFLAGS_QUEUE0NOTFULL_B 1
/* "domU->dom0 queue" is full */
#define NET_ACCEL_MSG_AFLAGS_QUEUEUFULL      0x4 
#define NET_ACCEL_MSG_AFLAGS_QUEUEUFULL_B    2
/* "domU->dom0 queue" is not full */
#define NET_ACCEL_MSG_AFLAGS_QUEUEUNOTFULL   0x8
#define NET_ACCEL_MSG_AFLAGS_QUEUEUNOTFULL_B 3
/* dom0 -> domU net_dev up/down events */
#define NET_ACCEL_MSG_AFLAGS_NETUPDOWN	 0x10
#define NET_ACCEL_MSG_AFLAGS_NETUPDOWN_B       4

/*
 * Masks used to test if there are any messages for domU and dom0
 * respectively
 */
#define NET_ACCEL_MSG_AFLAGS_TO_DOMU_MASK	\
	(NET_ACCEL_MSG_AFLAGS_QUEUE0FULL    |	\
	 NET_ACCEL_MSG_AFLAGS_QUEUEUNOTFULL |	\
	 NET_ACCEL_MSG_AFLAGS_NETUPDOWN)
#define NET_ACCEL_MSG_AFLAGS_TO_DOM0_MASK	\
	(NET_ACCEL_MSG_AFLAGS_QUEUE0NOTFULL |	\
	 NET_ACCEL_MSG_AFLAGS_QUEUEUFULL)

/*! The shared data structure used for inter-VM communication. */
struct net_accel_shared_page {
	/*! Sanity check */
	u32 magic;	    
	/*! Used by host/Dom0 */
	struct net_accel_msg_queue queue0;
	/*! Used by guest/DomU */
	struct net_accel_msg_queue queue1;
	/*! Atomic flags, used to communicate simple state changes */
	u32 aflags;     
	/*! State of net_dev used for acceleration */     
	u32 net_dev_up; 
};


enum net_accel_hw_type {
	/*! Not a virtualisable NIC: use slow path. */
	NET_ACCEL_MSG_HWTYPE_NONE = 0,
	/*! NIC is Falcon-based */
	NET_ACCEL_MSG_HWTYPE_FALCON_A = 1,
	NET_ACCEL_MSG_HWTYPE_FALCON_B = 2,
	NET_ACCEL_MSG_HWTYPE_SIENA_A = 3,
};

/*! The maximum number of pages used by an event queue. */
#define EF_HW_FALCON_EVQ_PAGES 8

struct net_accel_hw_falcon_b {
	/* VI */
	/*! Grant for Tx DMA Q */
	u32 txdmaq_gnt;   
	/*! Grant for Rx DMA Q */
	u32 rxdmaq_gnt;   
	/*! Machine frame number for Tx/Rx doorbell page */
	u32 doorbell_mfn; 
	/*! Grant for Tx/Rx doorbell page */
	u32 doorbell_gnt;

	/* Event Q */
	/*! Grants for the pages of the EVQ */
	u32 evq_mem_gnts[EF_HW_FALCON_EVQ_PAGES]; 
	u32 evq_offs;
	/*! log2(pages in event Q) */
	u32 evq_order;    
	/*! Capacity in events */
	u32 evq_capacity; 
	/*! Eventq pointer register physical address */
	u32 evq_rptr; 
	/*! Interface instance */
	u32 instance; 
	/*! Capacity of RX queue */
	u32 rx_capacity;
	/*! Capacity of TX queue */
	u32 tx_capacity;

	/* NIC */
	s32 nic_arch;
	s32 nic_revision;
	u8 nic_variant;
};

struct net_accel_hw_falcon_a {
	struct net_accel_hw_falcon_b common;
	u32 evq_rptr_gnt;
};


/*! Description of the hardware that the DomU is being given. */
struct net_accel_msg_hw {
	u32 type;		/*!< Hardware type */
	union {
		struct net_accel_hw_falcon_a falcon_a;
		struct net_accel_hw_falcon_b falcon_b;
	} resources;
};

/*! Start-of-day handshake message. Dom0 fills in its version and
 * sends, DomU checks, inserts its version and replies
 */
struct net_accel_msg_hello {
	/*! Sender's version (set by each side in turn) */
	u32 version;	
	/*! max pages allocated/allowed for buffers */
	u32 max_pages;      
};

/*! Maximum number of page requests that can fit in a message. */
#define NET_ACCEL_MSG_MAX_PAGE_REQ (8)

/*! Request for NIC buffers. DomU fils out pages and grants (and
 *  optionally) reqid, dom0 fills out buf and sends reply 
 */
struct net_accel_msg_map_buffers {
	u32 reqid;	/*!< Optional request ID */
	u32 pages;	/*!< Number of pages to map */
	u32 grants[NET_ACCEL_MSG_MAX_PAGE_REQ];  /*!< Grant ids to map */ 
	u32 buf;	  /*!< NIC buffer address of pages obtained */
};

/*! Notification of a change to local mac address, used to filter
  locally destined packets off the fast path */
struct net_accel_msg_localmac {
	u32 flags;	/*!< Should this be added or removed? */
	u8 mac[ETH_ALEN]; /*!< The mac address to filter onto slow path */
};

struct net_accel_msg_fastpath {
	u32 flags;	/*!< Should this be added or removed? */
	u8  mac[ETH_ALEN];/*!< The mac address to filter onto fast path */
	u16 port;	 /*!< The port of the connection */
	u32 ip;	   /*!< The IP address of the connection */
	u8  proto;	/*!< The protocol of connection (TCP/UDP) */
};

/*! Values for struct ef_msg_localmac/fastpath.flags */
#define NET_ACCEL_MSG_ADD    0x1
#define NET_ACCEL_MSG_REMOVE 0x2

/*! Overall message structure */
struct net_accel_msg {
	/*! ID specifying type of messge */
	u32 id;		     
	union {
		/*! handshake */
		struct net_accel_msg_hello hello;  
		/*! hardware description */
		struct net_accel_msg_hw hw;	
		/*! buffer map request */
		struct net_accel_msg_map_buffers mapbufs; 
		/*! mac address of a local interface */
		struct net_accel_msg_localmac localmac; 
		/*! address of a new fastpath connection */
		struct net_accel_msg_fastpath fastpath; 
		/*! make the message a fixed size */
		u8 pad[128 - sizeof(u32)]; 
	}  u;
};


#define NET_ACCEL_MSG_HW_TO_MSG(_u) container_of(_u, struct net_accel_msg, u.hw)

/*! Inter-domain message FIFO */
typedef struct {
	struct net_accel_msg *fifo;
	u32 fifo_mask;
	u32 *fifo_rd_i;
	u32 *fifo_wr_i;
	spinlock_t lock;
	u32 is_locked; /* Debug flag */
} sh_msg_fifo2;


#define NET_ACCEL_MSG_OFFSET_MASK PAGE_MASK

/* Modifiers */
#define NET_ACCEL_MSG_REPLY    (0x80000000)
#define NET_ACCEL_MSG_ERROR    (0x40000000)

/* Dom0 -> DomU and reply. Handshake/version check. */
#define NET_ACCEL_MSG_HELLO    (0x00000001)
/* Dom0 -> DomU : hardware setup (VI info.) */
#define NET_ACCEL_MSG_SETHW    (0x00000002)
/*
 * Dom0 -> DomU. Notification of a local mac to add/remove from slow
 * path filter
 */
#define NET_ACCEL_MSG_LOCALMAC (0x00000003)
/* 
 * DomU -> Dom0 and reply. Request for buffer table entries for
 * preallocated pages.
 */
#define NET_ACCEL_MSG_MAPBUF   (0x00000004)
/* 
 * Dom0 -> DomU. Notification of a local mac to add/remove from fast
 * path filter
 */
#define NET_ACCEL_MSG_FASTPATH (0x00000005)

/*! Initialise a message and set the type
 * \param message : the message
 * \param code : the message type 
 */
static inline void net_accel_msg_init(struct net_accel_msg *msg, int code) {
	msg->id = (u32)code;
}

/*! initialise a shared page structure
 * \param shared_page : mapped memory in which the structure resides
 * \param len : size of the message FIFO area that follows
 * \param up : initial up/down state of netdev 
 * \return 0 or an error code
 */
extern int net_accel_msg_init_page(void *shared_page, int len, int up);

/*! initialise a message queue 
 * \param queue : the message FIFO to initialise 
 * \param indices : the read and write indices in shared memory
 * \param base : the start of the memory area for the FIFO
 * \param size : the size of the FIFO in bytes
 */
extern void net_accel_msg_init_queue(sh_msg_fifo2 *queue,
				     struct net_accel_msg_queue *indices,
				     struct net_accel_msg *base, int size);

/* Notify after a batch of messages have been sent */
extern void net_accel_msg_notify(int irq);

/*! Send a message on the specified FIFO. The message is copied to the 
 *  current slot of the FIFO.
 * \param sp : pointer to shared page
 * \param q : pointer to message FIFO to use
 * \param msg : pointer to message 
 * \return 0 on success, -errno on
 */ 
extern int net_accel_msg_send(struct net_accel_shared_page *sp,
			      sh_msg_fifo2 *q, 
			      struct net_accel_msg *msg);
extern int net_accel_msg_reply(struct net_accel_shared_page *sp,
			      sh_msg_fifo2 *q, 
			      struct net_accel_msg *msg);

/*! As net_accel_msg_send but also posts a notification to the far end. */
extern int net_accel_msg_send_notify(struct net_accel_shared_page *sp, 
				     int irq, sh_msg_fifo2 *q, 
				     struct net_accel_msg *msg);
/*! As net_accel_msg_send but also posts a notification to the far end. */
extern int net_accel_msg_reply_notify(struct net_accel_shared_page *sp, 
				      int irq, sh_msg_fifo2 *q, 
				      struct net_accel_msg *msg);

/*! Receive a message on the specified FIFO. Returns 0 on success,
 *  -errno on failure.
 */
extern int net_accel_msg_recv(struct net_accel_shared_page *sp,
			      sh_msg_fifo2 *q,
			      struct net_accel_msg *msg);

/*! Look at a received message, if any, so a decision can be made
 *  about whether to read it now or not.  Cookie is a bit of debug
 *  which is set here and checked when passed to
 *  net_accel_msg_recv_next()
 */
extern int net_accel_msg_peek(struct net_accel_shared_page *sp,
			      sh_msg_fifo2 *queue, 
			      struct net_accel_msg *msg, int *cookie);
/*! Move the queue onto the next element, used after finished with a
 *  peeked msg 
 */
extern int net_accel_msg_recv_next(struct net_accel_shared_page *sp,
				   sh_msg_fifo2 *queue, int cookie);

/*! Start sending a message without copying. returns a pointer to a
 *  message that will be filled out in place. The queue is locked
 *  until the message is sent.
 */
extern 
struct net_accel_msg *net_accel_msg_start_send(struct net_accel_shared_page *sp,
					       sh_msg_fifo2 *queue,
					       unsigned long *flags);


/*! Complete the sending of a message started with
 *  net_accel_msg_start_send. The message is implicit since the queue
 *  was locked by _start 
 */
extern void net_accel_msg_complete_send(struct net_accel_shared_page *sp,
					sh_msg_fifo2 *queue,
					unsigned long *flags);

/*! As net_accel_msg_complete_send but does the notify. */
extern void net_accel_msg_complete_send_notify(struct net_accel_shared_page *sp, 
					       sh_msg_fifo2 *queue,
					       unsigned long *flags, int irq);

/*! Lock the queue so that multiple "_locked" functions can be called
 *  without the queue being modified by others 
 */
static inline
void net_accel_msg_lock_queue(sh_msg_fifo2 *queue, unsigned long *flags)
{
	spin_lock_irqsave(&queue->lock, (*flags));
	rmb();
	BUG_ON(queue->is_locked);
	queue->is_locked = 1;
}

/*! Unlock the queue */
static inline
void net_accel_msg_unlock_queue(sh_msg_fifo2 *queue, unsigned long *flags)
{
	BUG_ON(!queue->is_locked);
	queue->is_locked = 0;
	wmb();
	spin_unlock_irqrestore(&queue->lock, (*flags));
}

/*! Give up without sending a message that was started with
 *  net_accel_msg_start_send() 
 */
static inline 
void net_accel_msg_abort_send(struct net_accel_shared_page *sp,
			      sh_msg_fifo2 *queue, unsigned long *flags)
{
	net_accel_msg_unlock_queue(queue, flags);
}

/*! Test the queue to ensure there is sufficient space */
static inline
int net_accel_msg_check_space(sh_msg_fifo2 *queue, unsigned space)
{
	return sh_fifo2_space(queue) >= space;
}

#endif /* NET_ACCEL_MSG_IFACE_H */
