/*********************************************************************
 *                
 * Filename:      irlap.h
 * Version:       0.8
 * Description:   An IrDA LAP driver for Linux
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Aug  4 20:40:53 1997
 * Modified at:   Fri Dec 10 13:21:17 1999
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

#ifndef IRLAP_H
#define IRLAP_H

#include <linux/config.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/ppp_defs.h>
#include <linux/ppp-comp.h>
#include <linux/timer.h>

#include <net/irda/irlap_event.h>

#define CONFIG_IRDA_DYNAMIC_WINDOW 1

#define LAP_RELIABLE   1
#define LAP_UNRELIABLE 0

#define LAP_ADDR_HEADER 1  /* IrLAP Address Header */
#define LAP_CTRL_HEADER 1  /* IrLAP Control Header */

#define LAP_MAX_HEADER (LAP_ADDR_HEADER + LAP_CTRL_HEADER)

#define BROADCAST  0xffffffff /* Broadcast device address */
#define CBROADCAST 0xfe       /* Connection broadcast address */
#define XID_FORMAT 0x01       /* Discovery XID format */

#define LAP_WINDOW_SIZE 8
#define LAP_MAX_QUEUE  10

#define NR_EXPECTED     1
#define NR_UNEXPECTED   0
#define NR_INVALID     -1

#define NS_EXPECTED     1
#define NS_UNEXPECTED   0
#define NS_INVALID     -1

/* Main structure of IrLAP */
struct irlap_cb {
	irda_queue_t q;     /* Must be first */
	magic_t magic;

	/* Device we are attached to */
	struct net_device  *netdev;
	char		hw_name[2*IFNAMSIZ + 1];

	/* Connection state */
	volatile IRLAP_STATE state;       /* Current state */

	/* Timers used by IrLAP */
	struct timer_list query_timer;
	struct timer_list slot_timer;
	struct timer_list discovery_timer;
	struct timer_list final_timer;
	struct timer_list poll_timer;
	struct timer_list wd_timer;
	struct timer_list backoff_timer;

	/* Media busy stuff */
	struct timer_list media_busy_timer;
	int media_busy;

	/* Timeouts which will be different with different turn time */
	int slot_timeout;
	int poll_timeout;
	int final_timeout;
	int wd_timeout;

	struct sk_buff_head txq;  /* Frames to be transmitted */
	struct sk_buff_head txq_ultra;

 	__u8    caddr;        /* Connection address */
	__u32   saddr;        /* Source device address */
	__u32   daddr;        /* Destination device address */

	int     retry_count;  /* Times tried to establish connection */
	int     add_wait;     /* True if we are waiting for frame */

	__u8    connect_pending;
	__u8    disconnect_pending;

	/*  To send a faster RR if tx queue empty */
#ifdef CONFIG_IRDA_FAST_RR
	int     fast_RR_timeout;
	int     fast_RR;      
#endif /* CONFIG_IRDA_FAST_RR */
	
	int N1; /* N1 * F-timer = Negitiated link disconnect warning threshold */
	int N2; /* N2 * F-timer = Negitiated link disconnect time */
	int N3; /* Connection retry count */

	int     local_busy;
	int     remote_busy;
	int     xmitflag;

	__u8    vs;            /* Next frame to be sent */
	__u8    vr;            /* Next frame to be received */
	__u8    va;            /* Last frame acked */
 	int     window;        /* Nr of I-frames allowed to send */
	int     window_size;   /* Current negotiated window size */

#ifdef CONFIG_IRDA_DYNAMIC_WINDOW
	__u32   line_capacity; /* Number of bytes allowed to send */
	__u32   bytes_left;    /* Number of bytes still allowed to transmit */
#endif /* CONFIG_IRDA_DYNAMIC_WINDOW */

	struct sk_buff_head wx_list;

	__u8    ack_required;
	
	/* XID parameters */
 	__u8    S;           /* Number of slots */
	__u8    slot;        /* Random chosen slot */
 	__u8    s;           /* Current slot */
	int     frame_sent;  /* Have we sent reply? */

	hashbin_t   *discovery_log;
 	discovery_t *discovery_cmd;

	__u32 speed;		/* Link speed */

	struct qos_info  qos_tx;   /* QoS requested by peer */
	struct qos_info  qos_rx;   /* QoS requested by self */
	struct qos_info *qos_dev;  /* QoS supported by device */

	notify_t notify; /* Callbacks to IrLMP */

	int    mtt_required;  /* Minumum turnaround time required */
	int    xbofs_delay;   /* Nr of XBOF's used to MTT */
	int    bofs_count;    /* Negotiated extra BOFs */
	int    next_bofs;     /* Negotiated extra BOFs after next frame */
};

extern hashbin_t *irlap;

/* 
 *  Function prototypes 
 */
int irlap_init(void);
void irlap_cleanup(void);

struct irlap_cb *irlap_open(struct net_device *dev, struct qos_info *qos,
			    char *	hw_name);
void irlap_close(struct irlap_cb *self);

void irlap_connect_request(struct irlap_cb *self, __u32 daddr, 
			   struct qos_info *qos, int sniff);
void irlap_connect_response(struct irlap_cb *self, struct sk_buff *skb);
void irlap_connect_indication(struct irlap_cb *self, struct sk_buff *skb);
void irlap_connect_confirm(struct irlap_cb *, struct sk_buff *skb);

void irlap_data_indication(struct irlap_cb *, struct sk_buff *, int unreliable);
void irlap_data_request(struct irlap_cb *, struct sk_buff *, int unreliable);

#ifdef CONFIG_IRDA_ULTRA
void irlap_unitdata_request(struct irlap_cb *, struct sk_buff *);
void irlap_unitdata_indication(struct irlap_cb *, struct sk_buff *);
#endif /* CONFIG_IRDA_ULTRA */

void irlap_disconnect_request(struct irlap_cb *);
void irlap_disconnect_indication(struct irlap_cb *, LAP_REASON reason);

void irlap_status_indication(struct irlap_cb *, int quality_of_link);

void irlap_test_request(__u8 *info, int len);

void irlap_discovery_request(struct irlap_cb *, discovery_t *discovery);
void irlap_discovery_confirm(struct irlap_cb *, hashbin_t *discovery_log);
void irlap_discovery_indication(struct irlap_cb *, discovery_t *discovery);

void irlap_reset_indication(struct irlap_cb *self);
void irlap_reset_confirm(void);

void irlap_update_nr_received(struct irlap_cb *, int nr);
int irlap_validate_nr_received(struct irlap_cb *, int nr);
int irlap_validate_ns_received(struct irlap_cb *, int ns);

int  irlap_generate_rand_time_slot(int S, int s);
void irlap_initiate_connection_state(struct irlap_cb *);
void irlap_flush_all_queues(struct irlap_cb *);
void irlap_change_speed(struct irlap_cb *self, __u32 speed, int now);
void irlap_wait_min_turn_around(struct irlap_cb *, struct qos_info *);

void irlap_init_qos_capabilities(struct irlap_cb *, struct qos_info *);
void irlap_apply_default_connection_parameters(struct irlap_cb *self);
void irlap_apply_connection_parameters(struct irlap_cb *self, int now);
void irlap_set_local_busy(struct irlap_cb *self, int status);

#define IRLAP_GET_HEADER_SIZE(self) 2 /* Will be different when we get VFIR */
#define IRLAP_GET_TX_QUEUE_LEN(self) skb_queue_len(&self->txq)

#endif
