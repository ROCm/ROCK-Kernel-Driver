/*********************************************************************
 *                
 * Filename:      irttp.h
 * Version:       1.0
 * Description:   Tiny Transport Protocol (TTP) definitions
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:31 1997
 * Modified at:   Sun Dec 12 13:09:07 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-1999 Dag Brattli <dagb@cs.uit.no>, 
 *     All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#ifndef IRTTP_H
#define IRTTP_H

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>

#include <net/irda/irda.h>
#include <net/irda/irlmp.h>
#include <net/irda/qos.h>
#include <net/irda/irqueue.h>

#define TTP_MAX_CONNECTIONS    LM_MAX_CONNECTIONS
#define TTP_HEADER             1
#define TTP_MAX_HEADER         (TTP_HEADER + LMP_MAX_HEADER)
#define TTP_SAR_HEADER         5
#define TTP_PARAMETERS         0x80
#define TTP_MORE               0x80

#define DEFAULT_INITIAL_CREDIT 14

#define TTP_LOW_THRESHOLD       4
#define TTP_HIGH_THRESHOLD     10
#define TTP_MAX_QUEUE          14

/* Some priorities for disconnect requests */
#define P_NORMAL    0
#define P_HIGH      1

#define TTP_SAR_DISABLE 0
#define TTP_SAR_UNBOUND 0xffffffff

/* Parameters */
#define TTP_MAX_SDU_SIZE 0x01

/*
 *  This structure contains all data assosiated with one instance of a TTP 
 *  connection.
 */
struct tsap_cb {
	irda_queue_t q;            /* Must be first */
	magic_t magic;        /* Just in case */

	__u8 stsap_sel;       /* Source TSAP */
	__u8 dtsap_sel;       /* Destination TSAP */

	struct lsap_cb *lsap; /* Corresponding LSAP to this TSAP */

	__u8 connected;       /* TSAP connected */
	 
	__u8 initial_credit;  /* Initial credit to give peer */

        int avail_credit;    /* Available credit to return to peer */
	int remote_credit;   /* Credit held by peer TTP entity */
	int send_credit;     /* Credit held by local TTP entity */
	
	struct sk_buff_head tx_queue; /* Frames to be transmitted */
	struct sk_buff_head rx_queue; /* Received frames */
	struct sk_buff_head rx_fragments;
	int tx_queue_lock;
	int rx_queue_lock;
	spinlock_t lock;

	notify_t notify;       /* Callbacks to client layer */

	struct net_device_stats stats;
	struct timer_list todo_timer; 
	
	__u32 max_seg_size;     /* Max data that fit into an IrLAP frame */
	__u8  max_header_size;

	int   rx_sdu_busy;     /* RxSdu.busy */
	__u32 rx_sdu_size;     /* Current size of a partially received frame */
	__u32 rx_max_sdu_size; /* Max receive user data size */

	int tx_sdu_busy;       /* TxSdu.busy */
	__u32 tx_max_sdu_size; /* Max transmit user data size */

	int close_pend;        /* Close, but disconnect_pend */
	int disconnect_pend;   /* Disconnect, but still data to send */
	struct sk_buff *disconnect_skb;
};

struct irttp_cb {
	magic_t    magic;	
	hashbin_t *tsaps;
};

int  irttp_init(void);
void irttp_cleanup(void);

struct tsap_cb *irttp_open_tsap(__u8 stsap_sel, int credit, notify_t *notify);
int irttp_close_tsap(struct tsap_cb *self);

int irttp_data_request(struct tsap_cb *self, struct sk_buff *skb);
int irttp_udata_request(struct tsap_cb *self, struct sk_buff *skb);

int irttp_connect_request(struct tsap_cb *self, __u8 dtsap_sel, 
			  __u32 saddr, __u32 daddr,
			  struct qos_info *qos, __u32 max_sdu_size, 
			  struct sk_buff *userdata);
int irttp_connect_response(struct tsap_cb *self, __u32 max_sdu_size, 
			    struct sk_buff *userdata);
int irttp_disconnect_request(struct tsap_cb *self, struct sk_buff *skb,
			     int priority);
void irttp_flow_request(struct tsap_cb *self, LOCAL_FLOW flow);
void irttp_status_indication(void *instance,
			     LINK_STATUS link, LOCK_STATUS lock);
struct tsap_cb *irttp_dup(struct tsap_cb *self, void *instance);

static __inline __u32 irttp_get_saddr(struct tsap_cb *self)
{
	return irlmp_get_saddr(self->lsap);
}

static __inline __u32 irttp_get_daddr(struct tsap_cb *self)
{
	return irlmp_get_daddr(self->lsap);
}

static __inline __u32 irttp_get_max_seg_size(struct tsap_cb *self)
{
	return self->max_seg_size;
}

extern struct irttp_cb *irttp;

#endif /* IRTTP_H */
