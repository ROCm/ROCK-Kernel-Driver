/*********************************************************************
 *                
 * Filename:      irttp.c
 * Version:       1.2
 * Description:   Tiny Transport Protocol (TTP) implementation
 * Status:        Stable
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:31 1997
 * Modified at:   Wed Jan  5 11:31:27 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-2000 Dag Brattli <dagb@cs.uit.no>, 
 *     All Rights Reserved.
 *     Copyright (c) 2000-2001 Jean Tourrilhes <jt@hpl.hp.com>
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

#include <linux/config.h>
#include <linux/skbuff.h>
#include <linux/init.h>

#include <asm/byteorder.h>
#include <asm/unaligned.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irlap.h>
#include <net/irda/irlmp.h>
#include <net/irda/parameters.h>
#include <net/irda/irttp.h>

static struct irttp_cb *irttp = NULL;

static void __irttp_close_tsap(struct tsap_cb *self);

static int irttp_data_indication(void *instance, void *sap, 
				 struct sk_buff *skb);
static int irttp_udata_indication(void *instance, void *sap, 
				  struct sk_buff *skb);
static void irttp_disconnect_indication(void *instance, void *sap,  
					LM_REASON reason, struct sk_buff *);
static void irttp_connect_indication(void *instance, void *sap, 
				     struct qos_info *qos, __u32 max_sdu_size,
				     __u8 header_size, struct sk_buff *skb);
static void irttp_connect_confirm(void *instance, void *sap, 
				  struct qos_info *qos, __u32 max_sdu_size, 
				  __u8 header_size, struct sk_buff *skb);
static void irttp_run_tx_queue(struct tsap_cb *self);
static void irttp_run_rx_queue(struct tsap_cb *self);

static void irttp_flush_queues(struct tsap_cb *self);
static void irttp_fragment_skb(struct tsap_cb *self, struct sk_buff *skb);
static void irttp_start_todo_timer(struct tsap_cb *self, int timeout);
static struct sk_buff *irttp_reassemble_skb(struct tsap_cb *self);
static int irttp_param_max_sdu_size(void *instance, irda_param_t *param, 
				    int get);

/* Information for parsing parameters in IrTTP */
static pi_minor_info_t pi_minor_call_table[] = {
	{ NULL, 0 },                                             /* 0x00 */
	{ irttp_param_max_sdu_size, PV_INTEGER | PV_BIG_ENDIAN } /* 0x01 */
};
static pi_major_info_t pi_major_call_table[] = {{ pi_minor_call_table, 2 }};
static pi_param_info_t param_info = { pi_major_call_table, 1, 0x0f, 4 };

/*
 * Function irttp_init (void)
 *
 *    Initialize the IrTTP layer. Called by module initialization code
 *
 */
int __init irttp_init(void)
{
	/* Initialize the irttp structure. */
	if (irttp == NULL) {
		irttp = kmalloc(sizeof(struct irttp_cb), GFP_KERNEL);
		if (irttp == NULL)
			return -ENOMEM;
	}
	memset(irttp, 0, sizeof(struct irttp_cb));
	
	irttp->magic = TTP_MAGIC;

	irttp->tsaps = hashbin_new(HB_LOCAL);
	if (!irttp->tsaps) {
		ERROR(__FUNCTION__ "(), can't allocate IrTTP hashbin!\n");
		return -ENOMEM;
	}
	
	return 0;
}

/*
 * Function irttp_cleanup (void)
 *
 *    Called by module destruction/cleanup code
 *
 */
#ifdef MODULE
void irttp_cleanup(void) 
{
	/* Check for main structure */
	ASSERT(irttp != NULL, return;);
	ASSERT(irttp->magic == TTP_MAGIC, return;);
	
	/*
	 *  Delete hashbin and close all TSAP instances in it
	 */
	hashbin_delete(irttp->tsaps, (FREE_FUNC) __irttp_close_tsap);

	irttp->magic = 0;
	
	/* De-allocate main structure */
	kfree(irttp);

	irttp = NULL;
}
#endif

/*
 * Function irttp_open_tsap (stsap, notify)
 *
 *    Create TSAP connection endpoint,
 */
struct tsap_cb *irttp_open_tsap(__u8 stsap_sel, int credit, notify_t *notify) 
{
	struct tsap_cb *self;
	struct lsap_cb *lsap;
	notify_t ttp_notify;

	ASSERT(irttp != NULL, return NULL;);
	ASSERT(irttp->magic == TTP_MAGIC, return NULL;);

	/* The IrLMP spec (IrLMP 1.1 p10) says that we have the right to
	 * use only 0x01-0x6F. Of course, we can use LSAP_ANY as well.
	 * JeanII */
	if((stsap_sel != LSAP_ANY) &&
	   ((stsap_sel < 0x01) || (stsap_sel >= 0x70))) {
		IRDA_DEBUG(0, __FUNCTION__ "(), invalid tsap!\n");
		return NULL;
	}

	self = kmalloc(sizeof(struct tsap_cb), GFP_ATOMIC);
	if (self == NULL) {
		IRDA_DEBUG(0, __FUNCTION__ "(), unable to kmalloc!\n");
		return NULL;
	}
	memset(self, 0, sizeof(struct tsap_cb));
	spin_lock_init(&self->lock);

	init_timer(&self->todo_timer);

	/* Initialize callbacks for IrLMP to use */
	irda_notify_init(&ttp_notify);
	ttp_notify.connect_confirm = irttp_connect_confirm;
	ttp_notify.connect_indication = irttp_connect_indication;
	ttp_notify.disconnect_indication = irttp_disconnect_indication;
	ttp_notify.data_indication = irttp_data_indication;
	ttp_notify.udata_indication = irttp_udata_indication;
	if(notify->status_indication != NULL)
		ttp_notify.status_indication = irttp_status_indication;
	ttp_notify.instance = self;
	strncpy(ttp_notify.name, notify->name, NOTIFY_MAX_NAME);

	self->magic = TTP_TSAP_MAGIC;
	self->connected = FALSE;

	skb_queue_head_init(&self->rx_queue);
	skb_queue_head_init(&self->tx_queue);
	skb_queue_head_init(&self->rx_fragments);
	/*
	 *  Create LSAP at IrLMP layer
	 */
	lsap = irlmp_open_lsap(stsap_sel, &ttp_notify, 0);
	if (lsap == NULL) {
		WARNING(__FUNCTION__ "(), unable to allocate LSAP!!\n");
		return NULL;
	}
	
	/*
	 *  If user specified LSAP_ANY as source TSAP selector, then IrLMP
	 *  will replace it with whatever source selector which is free, so
	 *  the stsap_sel we have might not be valid anymore
	 */
	self->stsap_sel = lsap->slsap_sel;
	IRDA_DEBUG(4, __FUNCTION__ "(), stsap_sel=%02x\n", self->stsap_sel);

	self->notify = *notify;
	self->lsap = lsap;

	hashbin_insert(irttp->tsaps, (irda_queue_t *) self, (int) self, NULL);

	if (credit > TTP_MAX_QUEUE)
		self->initial_credit = TTP_MAX_QUEUE;
	else
		self->initial_credit = credit;

	return self;	
}

/*
 * Function irttp_close (handle)
 *
 *    Remove an instance of a TSAP. This function should only deal with the
 *    deallocation of the TSAP, and resetting of the TSAPs values;
 *
 */
static void __irttp_close_tsap(struct tsap_cb *self)
{
	/* First make sure we're connected. */
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return;);

	irttp_flush_queues(self);

	del_timer(&self->todo_timer);

	/* This one won't be cleaned up if we are diconnect_pend + close_pend
	 * and we receive a disconnect_indication */
	if (self->disconnect_skb)
		dev_kfree_skb(self->disconnect_skb);

	self->connected = FALSE;
	self->magic = ~TTP_TSAP_MAGIC;

	kfree(self);
}

/*
 * Function irttp_close (self)
 *
 *    Remove TSAP from list of all TSAPs and then deallocate all resources
 *    associated with this TSAP
 *
 * Note : because we *free* the tsap structure, it is the responsability
 * of the caller to make sure we are called only once and to deal with
 * possible race conditions. - Jean II
 */
int irttp_close_tsap(struct tsap_cb *self)
{
	struct tsap_cb *tsap;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return -1;);

	/* Make sure tsap has been disconnected */
	if (self->connected) {
		/* Check if disconnect is not pending */
		if (!test_bit(0, &self->disconnect_pend)) {
			WARNING(__FUNCTION__ "(), TSAP still connected!\n");
			irttp_disconnect_request(self, NULL, P_NORMAL);
		}
		self->close_pend = TRUE;
		irttp_start_todo_timer(self, 1*HZ);

		return 0; /* Will be back! */
	}
	
	tsap = hashbin_remove(irttp->tsaps, (int) self, NULL);

	ASSERT(tsap == self, return -1;);

	/* Close corresponding LSAP */
	if (self->lsap) {
		irlmp_close_lsap(self->lsap);
		self->lsap = NULL;
	}

	__irttp_close_tsap(self);

	return 0;
}

/*
 * Function irttp_udata_request (self, skb)
 *
 *    Send unreliable data on this TSAP
 *
 */
int irttp_udata_request(struct tsap_cb *self, struct sk_buff *skb) 
{
	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return -1;);
	ASSERT(skb != NULL, return -1;);

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	/* Check that nothing bad happens */
	if ((skb->len == 0) || (!self->connected)) {
		IRDA_DEBUG(1, __FUNCTION__ "(), No data, or not connected\n");
		return -1;
	}
	
	if (skb->len > self->max_seg_size) {
		IRDA_DEBUG(1, __FUNCTION__ "(), UData is to large for IrLAP!\n");
		return -1;
	}
		    
	irlmp_udata_request(self->lsap, skb);
	self->stats.tx_packets++;

	return 0;
}

/*
 * Function irttp_data_request (handle, skb)
 *
 *    Queue frame for transmission. If SAR is enabled, fragement the frame 
 *    and queue the fragments for transmission
 */
int irttp_data_request(struct tsap_cb *self, struct sk_buff *skb) 
{
	__u8 *frame;

	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return -1;);
	ASSERT(skb != NULL, return -1;);

	/* Check that nothing bad happens */
	if ((skb->len == 0) || (!self->connected)) {
		WARNING(__FUNCTION__ "(), No data, or not connected\n");
		return -ENOTCONN;
	}

	/*  
	 *  Check if SAR is disabled, and the frame is larger than what fits
	 *  inside an IrLAP frame
	 */
	if ((self->tx_max_sdu_size == 0) && (skb->len > self->max_seg_size)) {
		ERROR(__FUNCTION__ 
		      "(), SAR disabled, and data is to large for IrLAP!\n");
		return -EMSGSIZE;
	}

	/* 
	 *  Check if SAR is enabled, and the frame is larger than the 
	 *  TxMaxSduSize 
	 */
	if ((self->tx_max_sdu_size != 0) && 
	    (self->tx_max_sdu_size != TTP_SAR_UNBOUND) && 
	    (skb->len > self->tx_max_sdu_size))
	{
		ERROR(__FUNCTION__ "(), SAR enabled, "
		      "but data is larger than TxMaxSduSize!\n");
		return -EMSGSIZE;
	}
	/* 
	 *  Check if transmit queue is full
	 */
	if (skb_queue_len(&self->tx_queue) >= TTP_MAX_QUEUE) {
		/*
		 *  Give it a chance to empty itself
		 */
		irttp_run_tx_queue(self);
		
		return -ENOBUFS;
	}
       
	/* Queue frame, or queue frame segments */
	if ((self->tx_max_sdu_size == 0) || (skb->len < self->max_seg_size)) {
		/* Queue frame */
		ASSERT(skb_headroom(skb) >= TTP_HEADER, return -1;);
		frame = skb_push(skb, TTP_HEADER);
		frame[0] = 0x00; /* Clear more bit */
		
		skb_queue_tail(&self->tx_queue, skb);
	} else {
		/*
		 *  Fragment the frame, this function will also queue the
		 *  fragments, we don't care about the fact the transmit
		 *  queue may be overfilled by all the segments for a little
		 *  while
		 */
		irttp_fragment_skb(self, skb);
	}

	/* Check if we can accept more data from client */
	if ((!self->tx_sdu_busy) && 
	    (skb_queue_len(&self->tx_queue) > TTP_HIGH_THRESHOLD)) {
		
		/* Tx queue filling up, so stop client */
		self->tx_sdu_busy = TRUE;
		
	 	if (self->notify.flow_indication) {
 			self->notify.flow_indication(self->notify.instance, 
						     self, FLOW_STOP);
		}
 	}
	
	/* Try to make some progress */
	irttp_run_tx_queue(self);
	
	return 0;
}

/*
 * Function irttp_run_tx_queue (self)
 *
 *    Transmit packets queued for transmission (if possible)
 *
 */
static void irttp_run_tx_queue(struct tsap_cb *self) 
{
	struct sk_buff *skb;
	unsigned long flags;
	int n;

	/* Get exclusive access to the tx queue, otherwise don't touch it */
	if (irda_lock(&self->tx_queue_lock) == FALSE)
		return;

	/* Try to send out frames as long as we have credits */
	while ((self->send_credit > 0) &&
	       (skb = skb_dequeue(&self->tx_queue)))
	{
		/* 
		 * Make sure we don't flood IrLAP with frames just because
		 * the remote device has given us a lot of credits
		 */
		if (irlmp_get_lap_tx_queue_len(self->lsap) > LAP_MAX_QUEUE) {
			/* Re-queue packet */
			skb_queue_head(&self->tx_queue, skb);

			/* Try later. Would be better if IrLAP could notify us */
			irttp_start_todo_timer(self, MSECS_TO_JIFFIES(10));
			
			break;
		}

		/*
		 *  Since we can transmit and receive frames concurrently, 
		 *  the code below is a critical region and we must assure that
		 *  nobody messes with the credits while we update them.
		 */
		spin_lock_irqsave(&self->lock, flags);

		n = self->avail_credit;
		self->avail_credit = 0;
		
		/* Only room for 127 credits in frame */
		if (n > 127) {
			self->avail_credit = n-127;
			n = 127;
		}
		self->remote_credit += n;
		self->send_credit--;

		spin_unlock_irqrestore(&self->lock, flags);

		/* 
		 *  More bit must be set by the data_request() or fragment() 
		 *  functions
		 */
		skb->data[0] |= (n & 0x7f);
		
		/* Detach from socket.
		 * The current skb has a reference to the socket that sent
		 * it (skb->sk). When we pass it to IrLMP, the skb will be
		 * stored in in IrLAP (self->wx_list). When we are within
		 * IrLAP, we loose the notion of socket, so we should not
		 * have a reference to a socket. So, we drop it here.
		 * 
		 * Why does it matter ?
		 * When the skb is freed (kfree_skb), if it is associated
		 * with a socket, it release buffer space on the socket
		 * (through sock_wfree() and sock_def_write_space()).
		 * If the socket no longer exist, we may crash. Hard.
		 * When we close a socket, we make sure that associated packets
		 * in IrTTP are freed. However, we have no way to cancel
		 * the packet that we have passed to IrLAP. So, if a packet
		 * remains in IrLAP (retry on the link or else) after we
		 * close the socket, we are dead !
		 * Jean II */
		if (skb->sk != NULL) {
			/* IrSOCK application, IrOBEX, ... */
			IRDA_DEBUG(4, __FUNCTION__ "() : Detaching SKB from socket.\n");

			/* That's the right way to do it - Jean II */
			skb_orphan(skb);
		} else {
			/* IrCOMM over IrTTP, IrLAN, ... */
			IRDA_DEBUG(4, __FUNCTION__ "() : Got SKB not attached to a socket.\n");
		}

		/* Pass the skb to IrLMP - done */
		irlmp_data_request(self->lsap, skb);
		self->stats.tx_packets++;

		/* Check if we can accept more frames from client */
		if ((self->tx_sdu_busy) && 
		    (skb_queue_len(&self->tx_queue) < TTP_LOW_THRESHOLD)) 
		{
			self->tx_sdu_busy = FALSE;
			
			if (self->notify.flow_indication)
				self->notify.flow_indication(
					self->notify.instance, self,
					FLOW_START);
		}
	}
	
	/* Reset lock */
	self->tx_queue_lock = 0;
}

/*
 * Function irttp_give_credit (self)
 *
 *    Send a dataless flowdata TTP-PDU and give available credit to peer
 *    TSAP
 */
void irttp_give_credit(struct tsap_cb *self) 
{
	struct sk_buff *tx_skb = NULL;
	unsigned long flags;
	int n;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return;);	

	IRDA_DEBUG(4, __FUNCTION__ "() send=%d,avail=%d,remote=%d\n", 
		   self->send_credit, self->avail_credit, self->remote_credit);
	
	/* Give credit to peer */
	tx_skb = dev_alloc_skb(64);
	if (!tx_skb)
		return;

	/* Reserve space for LMP, and LAP header */
	skb_reserve(tx_skb, self->max_header_size);

	/*
	 *  Since we can transmit and receive frames concurrently, 
	 *  the code below is a critical region and we must assure that
	 *  nobody messes with the credits while we update them.
	 */
	spin_lock_irqsave(&self->lock, flags);

	n = self->avail_credit;
	self->avail_credit = 0;
	
	/* Only space for 127 credits in frame */
	if (n > 127) {
		self->avail_credit = n - 127;
		n = 127;
	}
	self->remote_credit += n;

	spin_unlock_irqrestore(&self->lock, flags);

	skb_put(tx_skb, 1);
	tx_skb->data[0] = (__u8) (n & 0x7f);
	
	irlmp_data_request(self->lsap, tx_skb);
	self->stats.tx_packets++;
}

/*
 * Function irttp_udata_indication (instance, sap, skb)
 *
 *    Received some unit-data (unreliable)
 *
 */
static int irttp_udata_indication(void *instance, void *sap, 
				  struct sk_buff *skb) 
{
	struct tsap_cb *self;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	self = (struct tsap_cb *) instance;

	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return -1;);
	ASSERT(skb != NULL, return -1;);

	/* Just pass data to layer above */
	if (self->notify.udata_indication)
		self->notify.udata_indication(self->notify.instance, self,skb);
	else
		dev_kfree_skb(skb);

	self->stats.rx_packets++;

	return 0;
}

/*
 * Function irttp_data_indication (instance, sap, skb)
 *
 *    Receive segment from IrLMP. 
 *
 */
static int irttp_data_indication(void *instance, void *sap, 
				 struct sk_buff *skb)
{
	struct tsap_cb *self;
	int n;

	self = (struct tsap_cb *) instance;

	n = skb->data[0] & 0x7f;     /* Extract the credits */

	self->stats.rx_packets++;

	/* 
	 *  Data or dataless packet? Dataless frames contains only the 
	 *  TTP_HEADER. 
	 */
	if (skb->len > 1) {
		/* Deal with inbound credit */
		self->send_credit += n;
		self->remote_credit--;
		
		/* 
		 *  We don't remove the TTP header, since we must preserve the
		 *  more bit, so the defragment routing knows what to do
		 */
		skb_queue_tail(&self->rx_queue, skb);
	} else {
		self->send_credit += n;	/* Dataless flowdata TTP-PDU */
		dev_kfree_skb(skb);
	}

	irttp_run_rx_queue(self);

	/* 
	 *  Give avay some credits to peer? 
	 */
	if ((skb_queue_empty(&self->tx_queue)) && 
	    (self->remote_credit < TTP_LOW_THRESHOLD) && 
	    (self->avail_credit > 0)) 
	{
		/* Schedule to start immediately after this thread */
		irttp_start_todo_timer(self, 0);
		/*irttp_give_credit(self);*/
	}
	
	/* 
	 * If the peer device has given us some credits and we didn't have
         * anyone from before, the we need to shedule the tx queue?  
	 */
	if (self->send_credit == n) {
		/*irttp_run_tx_queue(self);*/
		irttp_start_todo_timer(self, 0);
	}
	return 0;
}

/*
 * Function irttp_status_indication (self, reason)
 *
 *    Status_indication, just pass to the higher layer...
 *
 */
void irttp_status_indication(void *instance,
			     LINK_STATUS link, LOCK_STATUS lock)
{
	struct tsap_cb *self;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	self = (struct tsap_cb *) instance;
	
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return;);
	
	/*
	 *  Inform service user if he has requested it
	 */
	if (self->notify.status_indication != NULL)
		self->notify.status_indication(self->notify.instance, 
					       link, lock);
	else
		IRDA_DEBUG(2, __FUNCTION__ "(), no handler\n");
}

/*
 * Function irttp_flow_request (self, command)
 *
 *    This funtion could be used by the upper layers to tell IrTTP to stop
 *    delivering frames if the receive queues are starting to get full, or 
 *    to tell IrTTP to start delivering frames again.
 */
void irttp_flow_request(struct tsap_cb *self, LOCAL_FLOW flow)
{
	IRDA_DEBUG(1, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return;);

	switch (flow) {
	case FLOW_STOP:
		IRDA_DEBUG(1, __FUNCTION__ "(), flow stop\n");
		self->rx_sdu_busy = TRUE;
		break;
	case FLOW_START:
		IRDA_DEBUG(1, __FUNCTION__ "(), flow start\n");
		self->rx_sdu_busy = FALSE;
		
		irttp_run_rx_queue(self);
		break;
	default:
		IRDA_DEBUG(1, __FUNCTION__ "(), Unknown flow command!\n");
	}
}
	
/*
 * Function irttp_connect_request (self, dtsap_sel, daddr, qos)
 *
 *    Try to connect to remote destination TSAP selector
 *
 */
int irttp_connect_request(struct tsap_cb *self, __u8 dtsap_sel, 
			  __u32 saddr, __u32 daddr,
			  struct qos_info *qos, __u32 max_sdu_size, 
			  struct sk_buff *userdata) 
{
	struct sk_buff *skb;
	__u8 *frame;
	__u8 n;
	
	IRDA_DEBUG(4, __FUNCTION__ "(), max_sdu_size=%d\n", max_sdu_size); 
	
	ASSERT(self != NULL, return -EBADR;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return -EBADR;);

	if (self->connected)
		return -EISCONN;
	
	/* Any userdata supplied? */
	if (userdata == NULL) {
		skb = dev_alloc_skb(64);
		if (!skb) 
			return -ENOMEM;
		
		/* Reserve space for MUX_CONTROL and LAP header */
		skb_reserve(skb, TTP_MAX_HEADER);
	} else {
		skb = userdata;
		/*  
		 *  Check that the client has reserved enough space for 
		 *  headers
		 */
		ASSERT(skb_headroom(userdata) >= TTP_MAX_HEADER, return -1;);
	}

	/* Initialize connection parameters */
	self->connected = FALSE;
	self->avail_credit = 0;
	self->rx_max_sdu_size = max_sdu_size;
	self->rx_sdu_size = 0;
	self->rx_sdu_busy = FALSE;
	self->dtsap_sel = dtsap_sel;

	n = self->initial_credit;

	self->remote_credit = 0;
	self->send_credit = 0;
	
	/*
	 *  Give away max 127 credits for now
	 */
	if (n > 127) {
		self->avail_credit=n-127;
		n = 127;
	}

	self->remote_credit = n;

	/* SAR enabled? */
	if (max_sdu_size > 0) {
		ASSERT(skb_headroom(skb) >= (TTP_MAX_HEADER + TTP_SAR_HEADER), 
		       return -1;);

		/* Insert SAR parameters */
		frame = skb_push(skb, TTP_HEADER+TTP_SAR_HEADER);
		
		frame[0] = TTP_PARAMETERS | n; 
		frame[1] = 0x04; /* Length */
		frame[2] = 0x01; /* MaxSduSize */
		frame[3] = 0x02; /* Value length */

		put_unaligned(cpu_to_be16((__u16) max_sdu_size), 
			      (__u16 *)(frame+4));
	} else {
		/* Insert plain TTP header */
		frame = skb_push(skb, TTP_HEADER);
		
		/* Insert initial credit in frame */
		frame[0] = n & 0x7f;
	}

	/* Connect with IrLMP. No QoS parameters for now */
	return irlmp_connect_request(self->lsap, dtsap_sel, saddr, daddr, qos, 
				     skb);
}

/*
 * Function irttp_connect_confirm (handle, qos, skb)
 *
 *    Sevice user confirms TSAP connection with peer. 
 *
 */
static void irttp_connect_confirm(void *instance, void *sap, 
				  struct qos_info *qos, __u32 max_seg_size,
				  __u8 max_header_size, struct sk_buff *skb) 
{
	struct tsap_cb *self;
	int parameters;
	int ret;
	__u8 plen;
	__u8 n;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");
	
	self = (struct tsap_cb *) instance;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return;);
	ASSERT(skb != NULL, return;);

	self->max_seg_size = max_seg_size - TTP_HEADER;
	self->max_header_size = max_header_size + TTP_HEADER;

	/*
	 *  Check if we have got some QoS parameters back! This should be the
	 *  negotiated QoS for the link.
	 */
	if (qos) {
		IRDA_DEBUG(4, "IrTTP, Negotiated BAUD_RATE: %02x\n", 
		       qos->baud_rate.bits);			
		IRDA_DEBUG(4, "IrTTP, Negotiated BAUD_RATE: %d bps.\n", 
		       qos->baud_rate.value);
	}

	n = skb->data[0] & 0x7f;
	
	IRDA_DEBUG(4, __FUNCTION__ "(), Initial send_credit=%d\n", n);
	
	self->send_credit = n;
	self->tx_max_sdu_size = 0;
	self->connected = TRUE;

	parameters = skb->data[0] & 0x80;	

	ASSERT(skb->len >= TTP_HEADER, return;);
	skb_pull(skb, TTP_HEADER);

	if (parameters) {
		plen = skb->data[0];

		ret = irda_param_extract_all(self, skb->data+1,
					     IRDA_MIN(skb->len-1, plen), 
					     &param_info);

		/* Any errors in the parameter list? */
		if (ret < 0) {
			WARNING(__FUNCTION__ 
				"(), error extracting parameters\n");
			dev_kfree_skb(skb);

			/* Do not accept this connection attempt */
			return;
		}
		/* Remove parameters */
		skb_pull(skb, IRDA_MIN(skb->len, plen+1));
	}
	
	IRDA_DEBUG(4, __FUNCTION__ "() send=%d,avail=%d,remote=%d\n", 
	      self->send_credit, self->avail_credit, self->remote_credit);

	IRDA_DEBUG(2, __FUNCTION__ "(), MaxSduSize=%d\n", self->tx_max_sdu_size);

	if (self->notify.connect_confirm) {
		self->notify.connect_confirm(self->notify.instance, self, qos,
					     self->tx_max_sdu_size,
					     self->max_header_size, skb);
	}
}

/*
 * Function irttp_connect_indication (handle, skb)
 *
 *    Some other device is connecting to this TSAP
 *
 */
void irttp_connect_indication(void *instance, void *sap, struct qos_info *qos,
			      __u32 max_seg_size, __u8 max_header_size, 
			      struct sk_buff *skb) 
{
	struct tsap_cb *self;
	struct lsap_cb *lsap;
	int parameters;
	int ret;
	__u8 plen;
	__u8 n;

	self = (struct tsap_cb *) instance;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return;);
 	ASSERT(skb != NULL, return;);

	lsap = (struct lsap_cb *) sap;

	self->max_seg_size = max_seg_size - TTP_HEADER;;
	self->max_header_size = max_header_size+TTP_HEADER;

	IRDA_DEBUG(4, __FUNCTION__ "(), TSAP sel=%02x\n", self->stsap_sel);

	/* Need to update dtsap_sel if its equal to LSAP_ANY */
	self->dtsap_sel = lsap->dlsap_sel;

	n = skb->data[0] & 0x7f;

	self->send_credit = n;
	self->tx_max_sdu_size = 0;
	
	parameters = skb->data[0] & 0x80;

	ASSERT(skb->len >= TTP_HEADER, return;);
	skb_pull(skb, TTP_HEADER);

	if (parameters) {
		plen = skb->data[0];
		
		ret = irda_param_extract_all(self, skb->data+1,
					     IRDA_MIN(skb->len-1, plen), 
					     &param_info);

		/* Any errors in the parameter list? */
		if (ret < 0) {
			WARNING(__FUNCTION__ 
				"(), error extracting parameters\n");
			dev_kfree_skb(skb);
			
			/* Do not accept this connection attempt */
			return;
		}

		/* Remove parameters */
		skb_pull(skb, IRDA_MIN(skb->len, plen+1));
	}

	if (self->notify.connect_indication) {
		self->notify.connect_indication(self->notify.instance, self, 
						qos, self->tx_max_sdu_size, 
						self->max_header_size, skb);
	} else
		dev_kfree_skb(skb);
}

/*
 * Function irttp_connect_response (handle, userdata)
 *
 *    Service user is accepting the connection, just pass it down to
 *    IrLMP!
 * 
 */
int irttp_connect_response(struct tsap_cb *self, __u32 max_sdu_size, 
			   struct sk_buff *userdata)
{
	struct sk_buff *skb;
	__u8 *frame;
	int ret;
	__u8 n;

	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return -1;);

	IRDA_DEBUG(4, __FUNCTION__ "(), Source TSAP selector=%02x\n", 
		   self->stsap_sel);
	
	/* Any userdata supplied? */
	if (userdata == NULL) {
		skb = dev_alloc_skb(64);
		if (!skb)
			return -ENOMEM;

		/* Reserve space for MUX_CONTROL and LAP header */
		skb_reserve(skb, TTP_MAX_HEADER);
	} else {
		skb = userdata;
		/*  
		 *  Check that the client has reserved enough space for 
		 *  headers
		 */
		ASSERT(skb_headroom(skb) >= TTP_MAX_HEADER, return -1;);
	}
	
	self->avail_credit = 0;
	self->remote_credit = 0;
	self->rx_max_sdu_size = max_sdu_size;
	self->rx_sdu_size = 0;
	self->rx_sdu_busy = FALSE;

	n = self->initial_credit;

	/* Frame has only space for max 127 credits (7 bits) */
	if (n > 127) {
		self->avail_credit = n - 127;
		n = 127;
	}

	self->remote_credit = n;
	self->connected = TRUE;

	/* SAR enabled? */
	if (max_sdu_size > 0) {
		ASSERT(skb_headroom(skb) >= (TTP_MAX_HEADER+TTP_SAR_HEADER), 
		       return -1;);
		
		/* Insert TTP header with SAR parameters */
		frame = skb_push(skb, TTP_HEADER+TTP_SAR_HEADER);
		
		frame[0] = TTP_PARAMETERS | n;
		frame[1] = 0x04; /* Length */

		/* irda_param_insert(self, IRTTP_MAX_SDU_SIZE, frame+1,  */
/* 				  TTP_SAR_HEADER, &param_info) */
		
		frame[2] = 0x01; /* MaxSduSize */
		frame[3] = 0x02; /* Value length */

		put_unaligned(cpu_to_be16((__u16) max_sdu_size), 
			      (__u16 *)(frame+4));
	} else {
		/* Insert TTP header */
		frame = skb_push(skb, TTP_HEADER);
		
		frame[0] = n & 0x7f;
	}
	 
	ret = irlmp_connect_response(self->lsap, skb);

	return ret;
}

/*
 * Function irttp_dup (self, instance)
 *
 *    Duplicate TSAP, can be used by servers to confirm a connection on a
 *    new TSAP so it can keep listening on the old one.
 */
struct tsap_cb *irttp_dup(struct tsap_cb *orig, void *instance) 
{
	struct tsap_cb *new;

	IRDA_DEBUG(1, __FUNCTION__ "()\n");

	if (!hashbin_find(irttp->tsaps, (int) orig, NULL)) {
		IRDA_DEBUG(0, __FUNCTION__ "(), unable to find TSAP\n");
		return NULL;
	}
	new = kmalloc(sizeof(struct tsap_cb), GFP_ATOMIC);
	if (!new) {
		IRDA_DEBUG(0, __FUNCTION__ "(), unable to kmalloc\n");
		return NULL;
	}
	/* Dup */
	memcpy(new, orig, sizeof(struct tsap_cb));
	new->notify.instance = instance;
	new->lsap = irlmp_dup(orig->lsap, new);

	/* Not everything should be copied */
	init_timer(&new->todo_timer);

	skb_queue_head_init(&new->rx_queue);
	skb_queue_head_init(&new->tx_queue);
	skb_queue_head_init(&new->rx_fragments);

	hashbin_insert(irttp->tsaps, (irda_queue_t *) new, (int) new, NULL);

	return new;
}

/*
 * Function irttp_disconnect_request (self)
 *
 *    Close this connection please! If priority is high, the queued data 
 *    segments, if any, will be deallocated first
 *
 */
int irttp_disconnect_request(struct tsap_cb *self, struct sk_buff *userdata, 
			     int priority)
{
	struct sk_buff *skb;
	int ret;

	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return -1;);

	/* Already disconnected? */
	if (!self->connected) {
		IRDA_DEBUG(4, __FUNCTION__ "(), already disconnected!\n");
		if (userdata)
			dev_kfree_skb(userdata);
		return -1;
	}

	/* Disconnect already pending ?
	 * We need to use an atomic operation to prevent reentry. This
	 * function may be called from various context, like user, timer
	 * for following a disconnect_indication() (i.e. net_bh).
	 * Jean II */
	if(test_and_set_bit(0, &self->disconnect_pend)) {
		IRDA_DEBUG(0, __FUNCTION__ "(), disconnect already pending\n");
		if (userdata)
			dev_kfree_skb(userdata);

		/* Try to make some progress */
		irttp_run_tx_queue(self);
		return -1;
	}

	/*
	 *  Check if there is still data segments in the transmit queue
	 */
	if (skb_queue_len(&self->tx_queue) > 0) {
		if (priority == P_HIGH) {
			/* 
			 *  No need to send the queued data, if we are 
			 *  disconnecting right now since the data will
			 *  not have any usable connection to be sent on
			 */
			IRDA_DEBUG(1, __FUNCTION__  "High priority!!()\n" );
			irttp_flush_queues(self);
		} else if (priority == P_NORMAL) {
			/* 
			 *  Must delay disconnect until after all data segments
			 *  have been sent and the tx_queue is empty
			 */
			/* We'll reuse this one later for the disconnect */
			self->disconnect_skb = userdata;  /* May be NULL */

			irttp_run_tx_queue(self);

			irttp_start_todo_timer(self, MSECS_TO_JIFFIES(1000));
			return -1;
		}
	}
	IRDA_DEBUG(1, __FUNCTION__ "(), Disconnecting ...\n");
	self->connected = FALSE;

	if (!userdata) {
		skb = dev_alloc_skb(64);
		if (!skb)
			return -ENOMEM;
		
		/* 
		 *  Reserve space for MUX and LAP header 
		 */
		skb_reserve(skb, TTP_MAX_HEADER);
		
		userdata = skb;
	}
	ret = irlmp_disconnect_request(self->lsap, userdata);

	/* The disconnect is no longer pending */
	clear_bit(0, &self->disconnect_pend);	/* FALSE */

	return ret;
}

/*
 * Function irttp_disconnect_indication (self, reason)
 *
 *    Disconnect indication, TSAP disconnected by peer?
 *
 */
void irttp_disconnect_indication(void *instance, void *sap, LM_REASON reason, 
				 struct sk_buff *skb) 
{
	struct tsap_cb *self;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	self = (struct tsap_cb *) instance;
	
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return;);
	
	/* Prevent higher layer to send more data */
	self->connected = FALSE;
	
	/* Check if client has already tried to close the TSAP */
	if (self->close_pend) {
		/* In this case, the higher layer is probably gone. Don't
		 * bother it and clean up the remains - Jean II */
		if (skb)
			dev_kfree_skb(skb);
		irttp_close_tsap(self);
		return;
	}

	/* If we are here, we assume that is the higher layer is still
	 * waiting for the disconnect notification and able to process it,
	 * even if he tried to disconnect. Otherwise, it would have already
	 * attempted to close the tsap and self->close_pend would be TRUE.
	 * Jean II */

	/* No need to notify the client if has already tried to disconnect */
	if(self->notify.disconnect_indication)
		self->notify.disconnect_indication(self->notify.instance, self,
						   reason, skb);
	else
		if (skb)
			dev_kfree_skb(skb);
}

/*
 * Function irttp_do_data_indication (self, skb)
 *
 *    Try to deliver reassebled skb to layer above, and requeue it if that
 *    for some reason should fail. We mark rx sdu as busy to apply back
 *    pressure is necessary.
 */
void irttp_do_data_indication(struct tsap_cb *self, struct sk_buff *skb)
{
	int err;

	/* Check if client has already tried to close the TSAP */
	if (self->close_pend) {
		dev_kfree_skb(skb);
		return;
	}

	err = self->notify.data_indication(self->notify.instance, self, skb);

	/* Usually the layer above will notify that it's input queue is
	 * starting to get filled by using the flow request, but this may
	 * be difficult, so it can instead just refuse to eat it and just
	 * give an error back 
	 */
	if (err == -ENOMEM) {
		IRDA_DEBUG(0, __FUNCTION__ "() requeueing skb!\n");

		/* Make sure we take a break */
		self->rx_sdu_busy = TRUE;
		
		/* Need to push the header in again */
		skb_push(skb, TTP_HEADER);
		skb->data[0] = 0x00; /* Make sure MORE bit is cleared */
		
		/* Put skb back on queue */
		skb_queue_head(&self->rx_queue, skb);
	}
}

/*
 * Function irttp_run_rx_queue (self)
 *
 *     Check if we have any frames to be transmitted, or if we have any
 *     available credit to give away.
 */
void irttp_run_rx_queue(struct tsap_cb *self) 
{
	struct sk_buff *skb;
	int more = 0;

	IRDA_DEBUG(2, __FUNCTION__ "() send=%d,avail=%d,remote=%d\n", 
		   self->send_credit, self->avail_credit, self->remote_credit);

	/* Get exclusive access to the rx queue, otherwise don't touch it */
	if (irda_lock(&self->rx_queue_lock) == FALSE)
		return;
	
	/*
	 *  Reassemble all frames in receive queue and deliver them
	 */
	while (!self->rx_sdu_busy && (skb = skb_dequeue(&self->rx_queue))) {
		self->avail_credit++;

		more = skb->data[0] & 0x80;

		/* Remove TTP header */
		skb_pull(skb, TTP_HEADER);

		/* Add the length of the remaining data */
		self->rx_sdu_size += skb->len;

		/*  
		 * If SAR is disabled, or user has requested no reassembly
		 * of received fragements then we just deliver them
		 * immediately. This can be requested by clients that
		 * implements byte streams without any message boundaries
		 */
		if (self->rx_max_sdu_size == TTP_SAR_DISABLE) {
			irttp_do_data_indication(self, skb);
			self->rx_sdu_size = 0;

			continue;
		}

		/* Check if this is a fragment, and not the last fragment */
		if (more) {
			/*  
			 *  Queue the fragment if we still are within the 
			 *  limits of the maximum size of the rx_sdu
			 */
			if (self->rx_sdu_size <= self->rx_max_sdu_size) {
				IRDA_DEBUG(4, __FUNCTION__ "(), queueing frag\n");
				skb_queue_tail(&self->rx_fragments, skb);
			} else {
				/* Free the part of the SDU that is too big */
				dev_kfree_skb(skb);
			}
			continue;
		}
		/*
		 *  This is the last fragment, so time to reassemble!
		 */
		if ((self->rx_sdu_size <= self->rx_max_sdu_size) ||
		    (self->rx_max_sdu_size == TTP_SAR_UNBOUND)) 
		{
			/* 
			 * A little optimizing. Only queue the fragment if
			 * there are other fragments. Since if this is the
			 * last and only fragment, there is no need to
			 * reassemble :-) 
			 */
			if (!skb_queue_empty(&self->rx_fragments)) {
				skb_queue_tail(&self->rx_fragments, 
					       skb);
				
				skb = irttp_reassemble_skb(self);
			}
			
			/* Now we can deliver the reassembled skb */
			irttp_do_data_indication(self, skb);
		} else {
			IRDA_DEBUG(1, __FUNCTION__ "(), Truncated frame\n");
			
			/* Free the part of the SDU that is too big */
			dev_kfree_skb(skb);

			/* Deliver only the valid but truncated part of SDU */
			skb = irttp_reassemble_skb(self);
			
			irttp_do_data_indication(self, skb);
		}
		self->rx_sdu_size = 0;
	}
	/* Reset lock */
	self->rx_queue_lock = 0;
}

/*
 * Function irttp_flush_queues (self)
 *
 *     Flushes (removes all frames) in transitt-buffer (tx_list)
 */
void irttp_flush_queues(struct tsap_cb *self)
{
	struct sk_buff* skb;
	
	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return;);
	
	/* Deallocate frames waiting to be sent */
	while ((skb = skb_dequeue(&self->tx_queue)) != NULL)
		dev_kfree_skb(skb);
	
	/* Deallocate received frames */
	while ((skb = skb_dequeue(&self->rx_queue)) != NULL)
		dev_kfree_skb(skb);
	
	/* Deallocate received fragments */
	while ((skb = skb_dequeue(&self->rx_fragments)) != NULL)
		dev_kfree_skb(skb);
}

/*
 * Function irttp_reasseble (self)
 *
 *    Makes a new (continuous) skb of all the fragments in the fragment
 *    queue
 *
 */
static struct sk_buff *irttp_reassemble_skb(struct tsap_cb *self)
{
	struct sk_buff *skb, *frag;
	int n = 0;  /* Fragment index */
	
      	ASSERT(self != NULL, return NULL;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return NULL;);

	IRDA_DEBUG(2, __FUNCTION__ "(), self->rx_sdu_size=%d\n", 
		   self->rx_sdu_size);

	skb = dev_alloc_skb(TTP_HEADER + self->rx_sdu_size);
	if (!skb)
		return NULL;

	/* 
	 * Need to reserve space for TTP header in case this skb needs to 
	 * be requeued in case delivery failes
	 */
	skb_reserve(skb, TTP_HEADER);
	skb_put(skb, self->rx_sdu_size);

	/*
	 *  Copy all fragments to a new buffer
	 */
	while ((frag = skb_dequeue(&self->rx_fragments)) != NULL) {
		memcpy(skb->data+n, frag->data, frag->len);
		n += frag->len;
		
		dev_kfree_skb(frag);
	}
	IRDA_DEBUG(2, __FUNCTION__ "(), frame len=%d\n", n);

	IRDA_DEBUG(2, __FUNCTION__ "(), rx_sdu_size=%d\n", self->rx_sdu_size);
	ASSERT(n <= self->rx_sdu_size, return NULL;);

	/* Set the new length */
	skb_trim(skb, n);

	self->rx_sdu_size = 0;

	return skb;
}

/*
 * Function irttp_fragment_skb (skb)
 *
 *    Fragments a frame and queues all the fragments for transmission
 *
 */
static void irttp_fragment_skb(struct tsap_cb *self, struct sk_buff *skb)
{
	struct sk_buff *frag;
	__u8 *frame;

	IRDA_DEBUG(2, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return;);
	ASSERT(skb != NULL, return;);

	/*
	 *  Split frame into a number of segments
	 */
	while (skb->len > self->max_seg_size) {
		IRDA_DEBUG(2, __FUNCTION__  "(), fragmenting ...\n");

		/* Make new segment */
		frag = dev_alloc_skb(self->max_seg_size+self->max_header_size);
		if (!frag)
			return;

		skb_reserve(frag, self->max_header_size);

		/* Copy data from the original skb into this fragment. */
		memcpy(skb_put(frag, self->max_seg_size), skb->data, 
		       self->max_seg_size);

		/* Insert TTP header, with the more bit set */
		frame = skb_push(frag, TTP_HEADER);
		frame[0] = TTP_MORE;
		
		/* Hide the copied data from the original skb */
		skb_pull(skb, self->max_seg_size);

		/* Queue fragment */
		skb_queue_tail(&self->tx_queue, frag);
	}
	/* Queue what is left of the original skb */
	IRDA_DEBUG(2, __FUNCTION__  "(), queuing last segment\n");
	
	frame = skb_push(skb, TTP_HEADER);
	frame[0] = 0x00; /* Clear more bit */

	/* Queue fragment */
	skb_queue_tail(&self->tx_queue, skb);
}

/*
 * Function irttp_param_max_sdu_size (self, param)
 *
 *    Handle the MaxSduSize parameter in the connect frames, this function
 *    will be called both when this parameter needs to be inserted into, and
 *    extracted from the connect frames
 */
static int irttp_param_max_sdu_size(void *instance, irda_param_t *param, 
				    int get)
{
	struct tsap_cb *self;

	self = (struct tsap_cb *) instance;

	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return -1;);

	if (get)
		param->pv.i = self->tx_max_sdu_size;
	else
		self->tx_max_sdu_size = param->pv.i;

	IRDA_DEBUG(1, __FUNCTION__ "(), MaxSduSize=%d\n", param->pv.i);
	
	return 0;
}

/*
 * Function irttp_todo_expired (data)
 *
 *    Todo timer has expired!
 *
 */
static void irttp_todo_expired(unsigned long data)
{
	struct tsap_cb *self = (struct tsap_cb *) data;

	/* Check that we still exist */
	if (!self || self->magic != TTP_TSAP_MAGIC)
		return;
	
	irttp_run_rx_queue(self);
	irttp_run_tx_queue(self);
		
	/*  Give avay some credits to peer?  */
	if ((self->remote_credit < TTP_LOW_THRESHOLD) && 
	    (self->avail_credit > 0) && (skb_queue_empty(&self->tx_queue)))
	{
		irttp_give_credit(self);
	}

	/* Check if time for disconnect */
	if (test_bit(0, &self->disconnect_pend)) {
		/* Check if it's possible to disconnect yet */
		if (skb_queue_empty(&self->tx_queue)) {
			/* Make sure disconnect is not pending anymore */
			clear_bit(0, &self->disconnect_pend);	/* FALSE */

			/* Note : self->disconnect_skb may be NULL */
			irttp_disconnect_request(self, self->disconnect_skb,
						 P_NORMAL);
			self->disconnect_skb = NULL;
		} else {
			/* Try again later */
			irttp_start_todo_timer(self, 1*HZ);
			
			/* No reason to try and close now */
			return;
		}
	}
	
	/* Check if it's closing time */
	if (self->close_pend)
		irttp_close_tsap(self);
}

/*
 * Function irttp_start_todo_timer (self, timeout)
 *
 *    Start todo timer. 
 *
 */
static void irttp_start_todo_timer(struct tsap_cb *self, int timeout)
{
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == TTP_TSAP_MAGIC, return;);

	del_timer(&self->todo_timer);
	
	self->todo_timer.data     = (unsigned long) self;
	self->todo_timer.function = &irttp_todo_expired;
	self->todo_timer.expires  = jiffies + timeout;

	add_timer(&self->todo_timer);
}

#ifdef CONFIG_PROC_FS
/*
 * Function irttp_proc_read (buf, start, offset, len, unused)
 *
 *    Give some info to the /proc file system
 */
int irttp_proc_read(char *buf, char **start, off_t offset, int len)
{
	struct tsap_cb *self;
	unsigned long flags;
	int i = 0;
	
	ASSERT(irttp != NULL, return 0;);
	
	len = 0;
	
	save_flags(flags);
	cli();

	self = (struct tsap_cb *) hashbin_get_first(irttp->tsaps);
	while (self != NULL) {
		if (!self || self->magic != TTP_TSAP_MAGIC)
			break;

		len += sprintf(buf+len, "TSAP %d, ", i++);
		len += sprintf(buf+len, "stsap_sel: %02x, ", 
			       self->stsap_sel);
		len += sprintf(buf+len, "dtsap_sel: %02x\n", 
			       self->dtsap_sel);
		len += sprintf(buf+len, "  connected: %s, ",
			       self->connected? "TRUE":"FALSE");
		len += sprintf(buf+len, "avail credit: %d, ",
			       self->avail_credit);
		len += sprintf(buf+len, "remote credit: %d, ",
			       self->remote_credit);
		len += sprintf(buf+len, "send credit: %d\n",
			       self->send_credit);
		len += sprintf(buf+len, "  tx packets: %ld, ",
			       self->stats.tx_packets);
		len += sprintf(buf+len, "rx packets: %ld, ",
			       self->stats.rx_packets);
		len += sprintf(buf+len, "tx_queue len: %d ", 
			       skb_queue_len(&self->tx_queue));
		len += sprintf(buf+len, "rx_queue len: %d\n", 
			       skb_queue_len(&self->rx_queue));
		len += sprintf(buf+len, "  tx_sdu_busy: %s, ",
			       self->tx_sdu_busy? "TRUE":"FALSE");
		len += sprintf(buf+len, "rx_sdu_busy: %s\n",
			       self->rx_sdu_busy? "TRUE":"FALSE");
		len += sprintf(buf+len, "  max_seg_size: %d, ",
			       self->max_seg_size);
		len += sprintf(buf+len, "tx_max_sdu_size: %d, ",
			       self->tx_max_sdu_size);
		len += sprintf(buf+len, "rx_max_sdu_size: %d\n",
			       self->rx_max_sdu_size);

		len += sprintf(buf+len, "  Used by (%s)\n", 
				self->notify.name);

		len += sprintf(buf+len, "\n");
		
		self = (struct tsap_cb *) hashbin_get_next(irttp->tsaps);
	}
	restore_flags(flags);

	return len;
}

#endif /* PROC_FS */
