/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2002 International Business Machines, Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 * 
 * $Header: /cvsroot/lksctp/lksctp/sctp_cvs/net/sctp/sctp_ulpqueue.c,v 1.13 2002/07/12 14:50:26 jgrimm Exp $
 * 
 * This abstraction carries sctp events to the ULP (sockets).
 * 
 * The SCTP reference implementation is free software; 
 * you can redistribute it and/or modify it under the terms of 
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * The SCTP reference implementation is distributed in the hope that it 
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.  
 * 
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 * 
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by: 
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Sridhar Samudrala     <sri@us.ibm.com>
 * 
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */
static char *cvs_id __attribute__ ((unused)) = "$Id: sctp_ulpqueue.c,v 1.13 2002/07/12 14:50:26 jgrimm Exp $";

#include <linux/config.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/sctp/sctp_structs.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sctp_sm.h>

/* Forward declarations for internal helpers. */ 
static inline sctp_ulpevent_t * 
sctp_ulpqueue_reasm(sctp_ulpqueue_t *ulpq, sctp_ulpevent_t *event);
static inline sctp_ulpevent_t *
sctp_ulpqueue_order(sctp_ulpqueue_t *ulpq, sctp_ulpevent_t *event);


/* 1st Level Abstractions */

/* Create a new ULP queue.
 */
sctp_ulpqueue_t *
sctp_ulpqueue_new(sctp_association_t *asoc, uint16_t inbound, int priority)
{
	sctp_ulpqueue_t *ulpq;
	size_t size;
	
	/* Today, there is only a fixed size of storage needed for
	 * stream support, but make the interfaces acceptable for 
	 * the future.
	 */ 
	size = sizeof(sctp_ulpqueue_t)+sctp_ulpqueue_storage_size(inbound);
	ulpq = kmalloc(size, priority);

	if (NULL == ulpq) {
		goto fail;
	}

	if (NULL == sctp_ulpqueue_init(ulpq, asoc, inbound)) {
		goto fail_init;
	}

	ulpq->malloced = 1;
	return(ulpq);
	
fail_init:
	kfree(ulpq);
fail:
	return NULL;
	
} /* sctp_ulpqueue_new() */

/* Initialize a ULP queue from a block of memory. */
sctp_ulpqueue_t *
sctp_ulpqueue_init(sctp_ulpqueue_t *ulpq, sctp_association_t *asoc, 
		   uint16_t inbound)
{
	memset(ulpq, 
	       sizeof(sctp_ulpqueue_t) + sctp_ulpqueue_storage_size(inbound), 
	       0x00);

	ulpq->asoc = asoc;
	spin_lock_init(&ulpq->lock);
	skb_queue_head_init(&ulpq->reasm);
	skb_queue_head_init(&ulpq->lobby);
	ulpq->malloced = 0;

	return(ulpq);

} /* sctp_ulpqueue_init() */

/* Flush the reassembly and ordering queues. */
void 
sctp_ulpqueue_flush(sctp_ulpqueue_t *ulpq)
{
	struct sk_buff *skb;
	sctp_ulpevent_t *event;
	
	while((skb = skb_dequeue(&ulpq->lobby))) {
		event = (sctp_ulpevent_t *)skb->cb;
		sctp_ulpevent_free(event);		
	}

       while((skb = skb_dequeue(&ulpq->reasm))) {
		event = (sctp_ulpevent_t *)skb->cb;
		sctp_ulpevent_free(event);		
	}

} /* sctp_ulpqueue_flush() */


/* Dispose of a ulpqueue. */
void
sctp_ulpqueue_free(sctp_ulpqueue_t *ulpq)
{

	sctp_ulpqueue_flush(ulpq);
	if (ulpq->malloced) {
		kfree(ulpq);
	}

} /* sctp_ulpqueue_free() */


/* Process an incoming DATA chunk. */
int
sctp_ulpqueue_tail_data(sctp_ulpqueue_t *ulpq, sctp_chunk_t *chunk, 
			int priority)
{
	struct sk_buff_head temp;
	sctp_data_chunk_t *hdr;	
	sctp_ulpevent_t *event;

	hdr = (sctp_data_chunk_t *)chunk->chunk_hdr;
       
	/* FIXME: Instead of event being the skb clone, we really should 
	 * have a new skb based chunk structure that we can convert to 
	 * an event.  Temporarily, I'm carrying a few chunk fields in
	 * the event to allow reassembly.  Its too painful to change
	 * everything at once.  --jgrimm
	 */

	event = sctp_ulpevent_make_rcvmsg(chunk->asoc, chunk, priority);

	if (!event) {
		return -ENOMEM;
	}

	/* Do reassembly if needed. */
	event = sctp_ulpqueue_reasm(ulpq, event);

	/* Do ordering if needed. */
	if (NULL != event) {
		/* Create a temporary list to collect chunks on. */
		skb_queue_head_init(&temp);
		skb_queue_tail(&temp, event->parent);

		event = sctp_ulpqueue_order(ulpq, event);
	}
	
	/* Send event to the ULP. */
	if (NULL != event) {	       
		sctp_ulpqueue_tail_event(ulpq, event);	
	}

	return(0);

} /* sctp_ulpqueue_tail_data() */


/* Add a new event for propogation to the ULP. */
int 
sctp_ulpqueue_tail_event(sctp_ulpqueue_t *ulpq, sctp_ulpevent_t *event)
{
	struct sock *sk;
	sk = ulpq->asoc->base.sk;

	/* If the socket is just going to throw this away, do not
	 * even try to deliver it.
	 */
	if (sk->dead || (sk->shutdown & RCV_SHUTDOWN)) {
		goto out_free;
	}       	

	/* Check if the user wishes to receive this event. */ 
	if (!sctp_ulpevent_is_enabled(event, &sctp_sk(sk)->subscribe)) {
		goto out_free;
	}
	
	/* If we are harvesting multiple skbs they will be 
	 * collected on a list. 
	 */
	if (event->parent->list) {
		sctp_skb_list_tail(event->parent->list, &sk->receive_queue);
	} else {		
		skb_queue_tail(&sk->receive_queue, event->parent);
	}
	
	wake_up_interruptible(sk->sleep);		

	return 1;

out_free:
	if (event->parent->list) {		
		skb_queue_purge(event->parent->list);
	} else {
		kfree_skb(event->parent);
	}
   
	return 0;

} /* sctp_ulpqueue_tail_event() */



/* 2nd Level Abstractions */

/* Helper function to store chunks that need to be reassembled. */
static inline void 
sctp_ulpqueue_store_reasm(sctp_ulpqueue_t *ulpq, sctp_ulpevent_t *event)
{
	struct sk_buff *pos, *tmp;
	sctp_ulpevent_t *cevent;
	uint32_t tsn, ctsn;
	int flags __attribute ((unused));
	
	tsn = event->sndrcvinfo.sinfo_tsn;

	sctp_spin_lock_irqsave(&ulpq->reasm.lock, flags);

	/* Find the right place in this list. We store them by TSN. */
	sctp_skb_for_each(pos, &ulpq->reasm, tmp) {
		cevent = (sctp_ulpevent_t *)pos->cb;	
		ctsn = cevent->sndrcvinfo.sinfo_tsn;

		if (TSN_lt(tsn, ctsn)) { break; }
	}
	
	/* If the queue is empty, we have a different function to call. */
	if (skb_peek(&ulpq->reasm)) {
		__skb_insert(event->parent, pos->prev, pos, &ulpq->reasm);
	} 
	else {
		__skb_queue_tail(&ulpq->reasm, event->parent);
	}

	sctp_spin_unlock_irqrestore(&ulpq->reasm.lock, flags);

} /* sctp_ulpqueue_store_reasm() */

/* Helper function to return an event corresponding to the reassembled 
 * datagram. 
 */
static inline sctp_ulpevent_t *
sctp_make_reassembled_event(struct sk_buff *f_frag, struct sk_buff *l_frag)
{
	struct sk_buff *pos;
	sctp_ulpevent_t *event;
	struct sk_buff *pnext;

	pos = f_frag->next;

	/* Set the first fragment's frag_list to point to the 2nd fragment. */
	skb_shinfo(f_frag)->frag_list = pos; 

	/* Remove the first fragment from the reassembly queue. */
	__skb_unlink(f_frag, f_frag->list);

	do {	
		pnext = pos->next;

		/* Remove the fragment from the reassembly queue. */
		__skb_unlink(pos, pos->list);

		/* Break if we have reached the last fragment. */ 
		if (pos == l_frag) { break; }

		pos->next = pnext;
		pos = pnext;
	} while (1);

	event = (sctp_ulpevent_t *)f_frag->cb;

	return (event);

} /* sctp_make_reassembled_event() */

/* Helper function to check if an incoming chunk has filled up the last 
 * missing fragment in a SCTP datagram and return the corresponding event. 
 */
static inline sctp_ulpevent_t *
sctp_ulpqueue_retrieve_reassembled(sctp_ulpqueue_t *ulpq)
{
	struct sk_buff *pos, *tmp;
	sctp_ulpevent_t *cevent;
	struct sk_buff *first_frag = NULL;
	uint32_t ctsn, next_tsn;
	int flags __attribute ((unused));
	sctp_ulpevent_t *retval = NULL;

	/* Initialized to 0 just to avoid compiler warning message. Will
	 * never be used with this value. It is referenced only after it 
	 * is set when we find the first fragment of a message.
	 */
	next_tsn = 0;

	sctp_spin_lock_irqsave(&ulpq->reasm.lock, flags);
	/* The chunks are held in the reasm queue sorted by TSN. 
	 * Walk through the queue sequentially and look for a sequence of
	 * fragmented chunks that complete a datagram. 
	 * 'first_frag' and next_tsn are reset when we find a chunk which
	 * is the first fragment of a datagram. Once these 2 fields are set
	 * we expect to find the remaining middle fragments and the last
	 * fragment in order. If not, first_frag is reset to NULL and we
	 * start the next pass when we find another first fragment. 
	 */
	sctp_skb_for_each(pos, &ulpq->reasm, tmp) {
		cevent = (sctp_ulpevent_t *)pos->cb;	
		ctsn = cevent->sndrcvinfo.sinfo_tsn;

		switch (cevent->chunk_flags & SCTP_DATA_FRAG_MASK) {
		case SCTP_DATA_FIRST_FRAG:
			first_frag = pos;
			next_tsn = ctsn+1;
			break;
		case SCTP_DATA_MIDDLE_FRAG:
			if ((first_frag) && (ctsn == next_tsn)) {
				next_tsn++;
			} else {
				first_frag = NULL;
			}
			break;
		case SCTP_DATA_LAST_FRAG:
			if ((first_frag) && (ctsn == next_tsn)) {
				retval = sctp_make_reassembled_event(
						first_frag, pos);	
			} else {
				first_frag = NULL;
			}
			break;
		}
	
		/* We have the reassembled event. There is no need to look 
		 * further. 
		 */
		if (retval) { break; }
	} 
	sctp_spin_unlock_irqrestore(&ulpq->reasm.lock, flags);

	return (retval);

} /* sctp_ulpqueue_retrieve_reassembled() */

/* Helper function to reassemble chunks. Hold chunks on the reasm queue that 
 * need reassembling. 
 */
static inline sctp_ulpevent_t * 
sctp_ulpqueue_reasm(sctp_ulpqueue_t *ulpq, sctp_ulpevent_t *event)
{
	sctp_ulpevent_t *retval = NULL;

	/* FIXME: We should be using some new chunk structure here 
	 * instead of carrying chunk fields in the event structure.
	 * This is temporary as it is too painful to change everything 
	 * at once.
	 */

        /* Check if this is part of a fragmented message. */
	if (SCTP_DATA_NOT_FRAG == (event->chunk_flags & SCTP_DATA_FRAG_MASK)) {
		return(event);
	}

	sctp_ulpqueue_store_reasm(ulpq, event);
	retval = sctp_ulpqueue_retrieve_reassembled(ulpq);

	return(retval);

} /* sctp_ulpqueue_reasm() */
 

/* Helper function to gather skbs that have possibly become
 * ordered by an an incoming chunk. 
 */
static inline void
sctp_ulpqueue_retrieve_ordered(sctp_ulpqueue_t *ulpq,
				sctp_ulpevent_t *event)
{
	struct sk_buff *pos, *tmp;
	sctp_ulpevent_t *cevent;
	uint16_t sid, csid;
	uint16_t ssn, cssn;
	int flags __attribute ((unused));

	sid = event->sndrcvinfo.sinfo_stream;
	ssn = event->sndrcvinfo.sinfo_ssn;

	/* We are holding the chunks by stream, by SSN. */
	sctp_spin_lock_irqsave(&ulpq->lobby.lock, flags);
	sctp_skb_for_each(pos, &ulpq->lobby, tmp) {
		cevent = (sctp_ulpevent_t *)pos->cb;	
		csid = cevent->sndrcvinfo.sinfo_stream;
		cssn = cevent->sndrcvinfo.sinfo_ssn;
		
		/* Have we gone too far? */
		if (csid > sid) { break; }

		/* Have we not gone far enough? */
		if (csid < sid) { continue; }

		if (cssn != ulpq->ssn[sid]) { break; }
		
		ulpq->ssn[sid]++;
		__skb_unlink(pos, pos->list);
		
		/* Attach all gathered skbs to the event. */		
		__skb_queue_tail(event->parent->list, pos);
				 
	} 
	sctp_spin_unlock_irqrestore(&ulpq->lobby.lock, flags);

} /* sctp_ulpqueue_retrieve_ordered() */


/* Helper function to store chunks needing ordering.  */
static inline void 
sctp_ulpqueue_store_ordered(sctp_ulpqueue_t *ulpq,
			    sctp_ulpevent_t *event)
{
	struct sk_buff *pos, *tmp;
	sctp_ulpevent_t *cevent;
	uint16_t sid, csid;
	uint16_t ssn, cssn;
	int flags __attribute ((unused));
	
	
	sid = event->sndrcvinfo.sinfo_stream;
	ssn = event->sndrcvinfo.sinfo_ssn;	

	sctp_spin_lock_irqsave(&ulpq->lobby.lock, flags);

	/* Find the right place in this list.  We store them by
	 * stream ID and then by SSN.
	 */
	sctp_skb_for_each(pos, &ulpq->lobby, tmp) {
		cevent = (sctp_ulpevent_t *)pos->cb;	
		csid = cevent->sndrcvinfo.sinfo_stream;
		cssn = cevent->sndrcvinfo.sinfo_ssn;

		if (csid > sid) { break; }
		if (csid == sid && SSN_lt(ssn, cssn)) { break;}
		
	}
	
	/* If the queue is empty, we have a different function to call. */
	if (skb_peek(&ulpq->lobby)) {
		__skb_insert(event->parent, pos->prev, pos, &ulpq->lobby);
	} 
	else {
		__skb_queue_tail(&ulpq->lobby, event->parent);
	}

	
	sctp_spin_unlock_irqrestore(&ulpq->lobby.lock, flags);

} /* sctp_ulpqueue_store_ordered() */

static inline sctp_ulpevent_t *
sctp_ulpqueue_order(sctp_ulpqueue_t *ulpq, sctp_ulpevent_t *event)
{
	uint16_t sid;
	uint16_t ssn;	


	/* FIXME: We should be using some new chunk structure here 
	 * instead of carrying chunk fields in the event structure.
	 * This is temporary as it is too painful to change everything 
	 * at once.
	 */       
	

       /* Check if this message needs ordering. */
       if (SCTP_DATA_UNORDERED & event->chunk_flags) {
	       return(event);	       
       }       

       /* Note: The stream ID must be verified before this routine. */
       sid = event->sndrcvinfo.sinfo_stream;
       ssn = event->sndrcvinfo.sinfo_ssn;

       /* Is this the expected SSN for this stream ID? */

       if (ssn != ulpq->ssn[sid]) {

	       /* We've received something out of order, so find where it
		* needs to be placed.  We order by stream and then by SSN.
		*/
       
	       sctp_ulpqueue_store_ordered(ulpq, event);

	       return(NULL);
       }

       /* Mark that the next chunk has been found. */
       ulpq->ssn[sid]++;

       /* Go find any other chunks that were waiting for
	* ordering.
	*/  
       
       sctp_ulpqueue_retrieve_ordered(ulpq, event);

       return(event);
 
} /* sctp_ulpqueue_order() */




